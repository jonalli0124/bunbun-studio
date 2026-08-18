"""Place the animation, instead of dropping it on the object's anchor point.

Sitting is not "stand where the chair stands". You staged the sit at (258,185) beside a chair that
was somewhere else in that room - the useful number is the OFFSET between the two, because that
offset is what makes him land ON the seat rather than at the chair's feet, and it stays true at
every other chair in the room.

So:
  - the editor's library now carries `roomObjects` as well as `stage`, which is the only way to
    know which object the animation was staged against;
  - bringing an animation in works out the offset from whichever object affords what it needs
    (nearest one, if several do);
  - and you can always just drag the ghost, because an inferred number is a starting point, not
    an answer. Arrow keys nudge it when no object is selected.
"""
import io, os

HERE = os.path.dirname(os.path.abspath(__file__))
done = []


def patch(path, edits):
    s = io.open(path, encoding="utf-8").read()
    for old, new, label in edits:
        if old not in s:
            print("  SKIP:", label)
            continue
        s = s.replace(old, new, 1)
        done.append(label)
        print("  ok:", label)
    tmp = path + ".tmp"
    io.open(tmp, "w", encoding="utf-8").write(s)
    os.replace(tmp, path)


patch(os.path.join(HERE, "src", "attach_editor.html"), [
    ("""  return {clip:p.clip, fps:p.fps, character:p.character, order:p.order, layers:p.layers,
          stage:p.stage};""",
     """  // roomObjects rides along too - not to rebuild the room, but so the assembler can work out
  // WHAT the stage was placed against and keep that relationship at a different chair.
  return {clip:p.clip, fps:p.fps, character:p.character, order:p.order, layers:p.layers,
          stage:p.stage, roomObjects:p.roomObjects};""",
     "editor: the library remembers what it was staged against"),
])

patch(os.path.join(HERE, "src", "scene_tool.html"), [
    # ---- work out the offset when the animation arrives
    ("""  const st=p.stage||{};
  const authored=(typeof st.scale==='number'&&st.scale>0);""",
     """  const st=p.stage||{};
  const authored=(typeof st.scale==='number'&&st.scale>0);
  // Where he sat, RELATIVE to the thing he sat on. If the editor recorded both, this is exact;
  // otherwise it starts at zero and you drag it.
  let place={dx:0, dy:0, from:'nothing to go on - drag him where he belongs'};
  if(st.x!=null && st.y!=null){
    const ro=(p.roomObjects||[]).filter(o=>o&&o.x!=null&&o.y!=null);
    const need=(rule&&rule.needs)||null;
    const wants=ro.filter(o=>(o.can||[]).includes(need));
    const pool=wants.length?wants:ro;
    let best=null, bd=1e9;
    for(const o of pool){ const d=Math.hypot(o.x-st.x,o.y-st.y); if(d<bd){bd=d;best=o} }
    if(best){ place={dx:Math.round(st.x-best.x), dy:Math.round(st.y-best.y),
                     from:'measured from the '+nice(best.prop||best.object)+' you staged it on'} }
    else place={dx:0, dy:0, from:'the editor saved no room objects to measure against'};
  }""",
     "infer the offset from how it was staged"),

    ("""    stage:{x:+st.x||null, y:+st.y||null, z:+st.z||null,
           scale:authored?st.scale:100, authored},""",
     """    stage:{x:+st.x||null, y:+st.y||null, z:+st.z||null,
           scale:authored?st.scale:100, authored},
    place,""",
     "keep the offset on the animation"),

    # ---- apply it wherever he goes
    ("""    } else p.spots.forEach(o=>put(o.x,o.y));""",
     """    } else p.spots.forEach(o=>put(o.x+(a.place?a.place.dx:0), o.y+(a.place?a.place.dy:0)));""",
     "ghosts sit where you placed them"),

    ("""      if(fl){ const xs=fl.pts.map(q=>q[0]), ys=fl.pts.map(q=>q[1]);
        put((Math.min(...xs)+Math.max(...xs))/2,(Math.min(...ys)+Math.max(...ys))/2); }
      else put(GAME.w/2, GAME.floor);""",
     """      const ox=a.place?a.place.dx:0, oy=a.place?a.place.dy:0;
      if(fl){ const xs=fl.pts.map(q=>q[0]), ys=fl.pts.map(q=>q[1]);
        put((Math.min(...xs)+Math.max(...xs))/2+ox,(Math.min(...ys)+Math.max(...ys))/2+oy); }
      else put(GAME.w/2+ox, GAME.floor+oy);""",
     "and when it may happen anywhere")
])

print("written;", len(done), "edits")
