"""Pick the face-on view by looking at the art, and let him choose by eye.

Jon: "why is my sit not the one i did in the editor? it is angled where the editor is face on".

Rendered the eight views and looked (which is the rule I keep relearning):

    Adult_Sit/south.png        angled, three-quarter
    Adult_Sit/south-east.png   FACE ON
    Adult_Idle/south.png       face on
    Adult_Idle/south-east.png  face on

so the compass labels are not consistent between states, and my default of "south" was angled for
the sit. No fixed default can be right when the labels themselves vary per clip.

So the face-on view is MEASURED: the camera-facing pose is the symmetric one, and mirroring each
view and comparing tells us which. And because a measurement can still be wrong on some future
pack, the picker now shows the eight views as thumbnails, so choosing is a matter of looking
rather than of trusting a compass point.
"""
import io, os

HERE = os.path.dirname(os.path.abspath(__file__))
done = []


def patch(path, edits):
    s = io.open(path, encoding="utf-8").read()
    for i, e in enumerate(edits):
        old, new = e[0], e[1]
        label = e[2] if len(e) > 2 else f"{os.path.basename(path)} edit {i+1}"
        if old not in s:
            print("  SKIP:", label)
            continue
        s = s.replace(old, new, 1)
        done.append(label)
        print("  ok:", label)
    tmp = path + ".tmp"
    io.open(tmp, "w", encoding="utf-8").write(s)
    os.replace(tmp, path)


patch(os.path.join(HERE, "mkdata.py"), [
    ("""    if rotations:
        for x in fr: x["dir"]=os.path.splitext(x["name"])[0]""",
     """    if rotations:
        for x in fr: x["dir"]=os.path.splitext(x["name"])[0]
        # WHICH ONE FACES THE CAMERA. The labels are not consistent between states - Adult_Sit's
        # "south" is a three-quarter view while Adult_Idle's is face on - so it is measured
        # rather than assumed: the camera-facing pose is the symmetric one, so mirror each view
        # and keep whichever differs least from itself.
        best, bestScore = 0, None
        for idx, x in enumerate(fr):
            im = Image.open(os.path.join(d, x["name"])).convert("RGBA")
            bb = im.getchannel("A").point(lambda v: 255 if v > 127 else 0).getbbox()
            if not bb:
                continue
            crop = im.crop(bb)
            mir = crop.transpose(Image.FLIP_LEFT_RIGHT)
            a, b = crop.load(), mir.load()
            w, h = crop.size
            diff = 0
            for yy in range(0, h, 2):
                for xx in range(0, w, 2):
                    pa, pb = a[xx, yy], b[xx, yy]
                    if (pa[3] > 127) != (pb[3] > 127):
                        diff += 3           # a silhouette mismatch counts most
                    elif pa[3] > 127:
                        diff += (abs(pa[0]-pb[0]) + abs(pa[1]-pb[1]) + abs(pa[2]-pb[2])) / 255.0
            score = diff / max(1, (w // 2) * (h // 2))
            if bestScore is None or score < bestScore:
                best, bestScore = idx, score
        clips_face_on = best"""),

    ("""    clips[c]={"frames":fr,"rest":(max(ys) if ys else None),"rotations":rotations}""",
     """    clips[c]={"frames":fr,"rest":(max(ys) if ys else None),"rotations":rotations}
    if rotations:
        clips[c]["faceOn"]=fr[clips_face_on]["dir"]
        print("   %-14s faces the camera in %s" % (c, fr[clips_face_on]["dir"]))"""),
])


patch(os.path.join(HERE, "src", "scene_tool.html"), [
    # default to the measured view, not to a compass point
    ("""    ? {i:rotFrame(clip, (a && a.facing) || 'south'), cs:100}""",
     """    ? {i:rotFrame(clip, (a && a.facing) || faceOnOf(clip)), cs:100}"""),

    ("""function rotFrame(clip, want){
  const fr=SD.clips[clip].frames;
  const i=fr.findIndex(f=>f.dir===want);
  if(i>=0) return i;
  const j=fr.findIndex(f=>f.dir==='south');
  return j>=0?j:0;
}""",
     """// The view that faces the camera, measured from the art when the pack was built - the compass
// names are not consistent between states, so "south" is face on in the idle and angled in the
// sit. Never assume a direction name means what it says.
const faceOnOf=c=>(SD.clips[c]&&SD.clips[c].faceOn)||'south';
function rotFrame(clip, want){
  const fr=SD.clips[clip].frames;
  const i=fr.findIndex(f=>f.dir===want);
  if(i>=0) return i;
  const j=fr.findIndex(f=>f.dir===faceOnOf(clip));
  return j>=0?j:0;
}"""),

    # choose by eye, not by compass point
    ("""          <select id="facing" style="width:100%">
            <option value="south" selected>south &mdash; towards you</option>
            <option value="south-east">south-east</option>
            <option value="east">east</option>
            <option value="north-east">north-east</option>
            <option value="north">north &mdash; away from you</option>
            <option value="north-west">north-west</option>
            <option value="west">west</option>
            <option value="south-west">south-west</option>
          </select>""",
     """          <div id="facingPick" style="display:flex;flex-wrap:wrap;gap:4px"></div>"""),

    ("""  { const rot=isRot(a.clip);
    $('facingBox').style.display=rot?'':'none';
    if(rot) $('facing').value=a.facing||'south'; }""",
     """  { const rot=isRot(a.clip);
    $('facingBox').style.display=rot?'':'none';
    if(rot){
      const cur=a.facing||faceOnOf(a.clip);
      // eight thumbnails: pick the one that looks right rather than guessing at a compass point
      $('facingPick').innerHTML=SD.clips[a.clip].frames.map((f,i)=>
        `<button class="mini" data-dir="${f.dir}" title="${f.dir}${
            f.dir===faceOnOf(a.clip)?' - faces the camera':''}"
          style="padding:2px;line-height:0;${f.dir===cur
            ?'border-color:var(--accent);box-shadow:0 0 0 2px var(--accent)':''}"
        ><img src="${f.img}" style="width:34px;height:34px;image-rendering:pixelated"></button>`).join('');
      $('facingPick').querySelectorAll('[data-dir]').forEach(b=>b.onclick=()=>{
        a.facing=b.dataset.dir; syncRule(); draw() });
    } }"""),

    ("""$('facing').onchange=e=>{ const a=anims[asel]; if(!a)return;
  a.facing=e.target.value; draw() };""",
     """"""),

    # the rest beat should face the camera too, not a compass guess
    ("""    const facing=sim.face<0?'south-west':'south-east';""",
     """    const facing=faceOnOf(c);        // measured, not a compass point"""),
])

print("written;", len(done), "edits")
