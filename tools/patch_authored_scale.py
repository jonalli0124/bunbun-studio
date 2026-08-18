"""The animation's own size and depth, which the assembler was throwing away.

The editor writes a `stage` onto every animation - {x, y, z, scale}. That IS the answer to "the
scaling can come from the animation": "sit in the chair" was authored at scale 63, and the
assembler drew it at 100, so the capybara loomed over the chair he was supposed to be sitting in.

`stage.z` matters just as much. Without it the character always painted last, on top of every
object, so he sat in FRONT of the chair rather than in it.

Travel keeps its own dial, because nobody authored the walk.
"""
import io, os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "scene_tool.html")
s = io.open(SRC, encoding="utf-8").read()
done = []


def rep(old, new, label):
    global s
    if old not in s:
        print("  SKIP:", label)
        return
    s = s.replace(old, new, 1)
    done.append(label)
    print("  ok:", label)


rep("""  const a={name, clip, rule, on:true, fps:p.fps||7,""",
    """  // The editor's stage: where it was authored, how big, and how deep in the room. Scale is the
  // one that was being dropped, and it is the whole reason a sit came out oversized.
  const st=p.stage||{};
  const a={name, clip, rule, on:true, fps:p.fps||7,
    stage:{x:+st.x||null, y:+st.y||null, z:+st.z||null,
           scale:(typeof st.scale==='number'&&st.scale>0)?st.scale:100},""",
    "keep the authored stage")

rep("""  const clip = clipOver || a.clip;
  // clipOver is only ever set for a clip the assembler chose itself (walking, idling). An
  // animation you authored keeps the size the editor gave it, untouched.
  const sc=SCALE[phaseOf(clip)] * (clipOver ? travelScale : 1);""",
    """  const clip = clipOver || a.clip;
  // clipOver is only ever set for a clip the assembler chose itself (walking, idling) - nobody
  // authored those, so they take the travel dial. Everything else takes the size the editor
  // recorded on the animation, which is the only place that number can honestly come from.
  const authored=(a.stage&&a.stage.scale?a.stage.scale:100)/100;
  const sc=SCALE[phaseOf(clip)] * (clipOver ? travelScale : authored);""",
    "authored scale is honoured")

# --- depth: the character is a row in the same z-stack as everything else
rep("""  // the things in the room, back to front
  const items=[...scene.objects].sort((a,b)=>(a.z==null?a.y:a.z)-(b.z==null?b.y:b.z));""",
    """  // The character belongs IN this stack, not on top of it. An authored z (the editor's stage.z)
  // is what lets him sit in a chair rather than in front of it; travelling, his feet decide.
  const charZ=(()=>{
    if(simOn&&sim){ const st=sim.a&&sim.a.stage;
      return (sim.phase!=='walk' && st && st.z!=null) ? st.z : sim.y }
    const a=anims[asel]; const st=a&&a.stage;
    return (st&&st.z!=null) ? st.z : null })();
  let charDrawn=false;
  const paintChar=()=>{ if(charDrawn) return; charDrawn=true;
    if(simOn) drawSim(); else ghosts(); };

  // the things in the room, back to front
  const items=[...scene.objects].sort((a,b)=>(a.z==null?a.y:a.z)-(b.z==null?b.y:b.z));""",
    "the character joins the z-stack")

rep("""  for(const o of items){
    if(scene.objects[osel]===o){ const {w,h}=sizeOf(o);""",
    """  for(const o of items){
    if(charZ!=null && !charDrawn && (o.z==null?o.y:o.z) > charZ) paintChar();
    if(scene.objects[osel]===o){ const {w,h}=sizeOf(o);""",
    "paint him at his own depth")

# The ghost block was inline in draw(), between the objects and the zone list, so it could only
# ever paint on top. Wrap it in a closure and both paths become "paint the character here".
rep("""  drawSim();

  // the selected animation, ghosted at every place it could happen
  const a=anims[asel];
  if(a && a.on && !simOn){""",
    """  const ghosts=()=>{
  // the selected animation, ghosted at every place it could happen
  const a=anims[asel];
  if(a && a.on && !simOn){""",
    "open the ghost closure")

rep("""    } else p.spots.forEach(o=>put(o.x,o.y));
  }
  { const zs=scene.zones;""",
    """    } else p.spots.forEach(o=>put(o.x,o.y));
  }
  };
  paintChar();          // anything deeper than him has already been drawn; he goes on now
  { const zs=scene.zones;""",
    "close it and paint whatever is left")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
