"""A pose is not an animation. Seven clips are 8 compass views, and cycling them is a turntable.

  Adult_Sit  Adult_Sleep  Adult_Idle  Baby_Sit  Baby_Sleep  Baby_Crawl  Baby_Onesie

each hold east / north-east / north / north-west / west / south-west / south / south-east - the
STATES from the pipeline, generated in eight directions. Everything else (Angry, Bathe, Eat,
Walk_East...) really is a sequence of frames.

Nothing in the tools knew the difference, so a sit was played frame-by-frame and he rotated on the
spot. That is "he is also spinning on the chair", and it is the same reason the idle spun during
his rest beat.

  mkdata      tags a clip `rotations` and names the direction of each frame
  assembler   never cycles a rotation clip - it HOLDS one direction, chosen in the panel,
              defaulting to south (facing the viewer), and it says which
  editor       labels the strip with direction names, so it is visible that these are
              eight ways of facing rather than eight moments in time
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


# ---------------------------------------------------------------- the data knows
patch(os.path.join(HERE, "mkdata.py"), [
    ("""    ys=[x["mid"][1] for x in fr if x["mid"]]
    clips[c]={"frames":fr,"rest":(max(ys) if ys else None)}""",
     """    ys=[x["mid"][1] for x in fr if x["mid"]]
    # A STATE is generated as eight compass views, not as a sequence. Cycling one is a
    # turntable, so the tools have to be able to tell the two apart.
    DIRS=("east","north-east","north","north-west","west","south-west","south","south-east")
    names=[os.path.splitext(x["name"])[0] for x in fr]
    rotations=bool(names) and all(n in DIRS for n in names)
    if rotations:
        for x in fr: x["dir"]=os.path.splitext(x["name"])[0]
    clips[c]={"frames":fr,"rest":(max(ys) if ys else None),"rotations":rotations}"""),
])

# ---------------------------------------------------------------- the assembler holds a facing
patch(os.path.join(HERE, "src", "scene_tool.html"), [
    ("""function drawChar(a, step, x, y, alpha, mark, clipOver){
  const Z=GAME.z;""",
     """// Eight ways of facing, not eight moments. Sit, sleep, idle and the bare crawl are all
// states; playing them frame by frame spins him on the spot.
const isRot=c=>!!(SD.clips[c]&&SD.clips[c].rotations);
function rotFrame(clip, want){
  const fr=SD.clips[clip].frames;
  const i=fr.findIndex(f=>f.dir===want);
  if(i>=0) return i;
  const j=fr.findIndex(f=>f.dir==='south');
  return j>=0?j:0;
}
function drawChar(a, step, x, y, alpha, mark, clipOver){
  const Z=GAME.z;"""),

    ("""  const ent = clipOver ? {i:step % (SD.clips[clip]?SD.clips[clip].frames.length:1), cs:100}
                       : seq[((step%seq.length)+seq.length)%seq.length];""",
     """  // A rotation clip HOLDS its direction. Everything else advances a frame.
  const ent = isRot(clip)
    ? {i:rotFrame(clip, (a && a.facing) || 'south'), cs:100}
    : (clipOver ? {i:step % (SD.clips[clip]?SD.clips[clip].frames.length:1), cs:100}
                : seq[((step%seq.length)+seq.length)%seq.length]);"""),

    # a facing picker, shown only when it means something
    ("""        <div style="margin-top:8px">
          <label>drawn at <span id="asl">100</span>%""",
     """        <div id="facingBox" style="display:none;margin-top:8px">
          <label>facing <span class="hint">&mdash; this is a pose in 8 directions, not
            8 frames, so it holds one</span></label>
          <select id="facing" style="width:100%">
            <option value="south" selected>south &mdash; towards you</option>
            <option value="south-east">south-east</option>
            <option value="east">east</option>
            <option value="north-east">north-east</option>
            <option value="north">north &mdash; away from you</option>
            <option value="north-west">north-west</option>
            <option value="west">west</option>
            <option value="south-west">south-west</option>
          </select>
        </div>
        <div style="margin-top:8px">
          <label>drawn at <span id="asl">100</span>%"""),

    ("""  $('apFront').classList.toggle('pri', a.depth==='front');""",
     """  { const rot=isRot(a.clip);
    $('facingBox').style.display=rot?'':'none';
    if(rot) $('facing').value=a.facing||'south'; }
  $('apFront').classList.toggle('pri', a.depth==='front');"""),

    ("""$('apFront').onclick=()=>{""",
     """$('facing').onchange=e=>{ const a=anims[asel]; if(!a)return;
  a.facing=e.target.value; draw() };
$('apFront').onclick=()=>{"""),

    # the travelling idle should face him too, rather than cycling
    ("""  } else if(sim.phase==='rest'){
    const c=idleClip(sim.a);
    c ? drawChar(sim.a, Math.floor(sim.t*6), sim.x, sim.y, 1, false, c)
      : drawChar(sim.a, 0, sim.x, sim.y, 1, false);""",
     """  } else if(sim.phase==='rest'){
    const c=idleClip(sim.a);
    // Adult_Idle is a rotation set too - hold it facing the way he last walked, rather than
    // letting him revolve while he thinks about what to do next.
    const facing=sim.face<0?'south-west':'south-east';
    if(c && isRot(c)) drawChar({...sim.a, facing}, 0, sim.x, sim.y, 1, false, c);
    else if(c) drawChar(sim.a, Math.floor(sim.t*6), sim.x, sim.y, 1, false, c);
    else drawChar(sim.a, 0, sim.x, sim.y, 1, false);"""),
])

# ---------------------------------------------------------------- and the editor says so
patch(os.path.join(HERE, "src", "attach_editor.html"), [
    ("function animCore(){",
     """// Visible in the strip: these are directions, not moments. Authoring a "sit" by stepping
// through them produces a turntable, which is exactly what happened.
const CLIP_IS_ROTATIONS = c => !!(DATA && DATA.clips[c] && DATA.clips[c].rotations);
function animCore(){"""),
])

print("written;", len(done), "edits")
