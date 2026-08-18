"""Furniture blocks walking BY ITSELF, exactly as the firmware's clearOfBlocks does.

Jon: "he keeps walking through the bath tub in the middle" and "he sometimes magically goes from
behind the tub to the other side". One cause: his keep-out is narrower than the tub's art, so the
strips beside and behind it are legal ground - routes thread them, and while he is back there the
tub draws OVER him, so he vanishes and re-emerges on the far side. Hand-drawn fences can never
quite match the art.

main.cpp:1815 settles it: every item IS a block - its column, down to its front edge - and a
destination inside one gets pushed out front. Ported:

    an object's footprint (x within its width, y from its top to its base line) is not walkable
    ...except the one he is on his way to USE, same licence as its keep-out
    he may never STOP inside one
    wall-mounted things (sconces) sit above the floor, so their blocks touch nothing he walks

Keep-outs stay for what only they can say: "keep off this PATCH OF FLOOR" - rugs, doorways,
the cat's spot - not furniture outlines.
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


rep("""// The zone he is currently allowed into, because it is the one around the thing he is going to
// use. Set for the length of a route calculation and cleared afterwards.
let routeAllow=-1;""",
    """// The zone he is currently allowed into, because it is the one around the thing he is going to
// use. Set for the length of a route calculation and cleared afterwards.
let routeAllow=-1;
// ...and the same licence for the OBJECT he is going to use (its oid).
let allowObj=-1;
// main.cpp:1815 clearOfBlocks - every item is a block: its column, top to base line. Walking
// behind or through furniture is what made him vanish behind the tub and "magically" come out
// the other side; the art draws over anything whose feet are above its base.
function blockObjAt(x,y){
  for(const o of scene.objects){
    if(o.hidden) continue;
    const d=SD.props[o.object]; if(!d) continue;
    const {w,h}=sizeOf(o);
    if(x>=o.x-w/2 && x<=o.x+w/2 && y<=o.y && y>=o.y-h) return o.oid==null?-2:o.oid;
  }
  return -1;
}""",
    "objects are blocks")

rep("""function walkable(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  // He walks AROUND the bath in the middle of the room - that is what its zone is for. The one
  // exception is the zone around whatever he is on his way to use.
  const z=zoneAt(x,y);
  return z<0 || z===routeAllow;
}""",
    """function walkable(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  // He walks AROUND the bath in the middle of the room - that is what its zone is for. The one
  // exception is the zone around whatever he is on his way to use.
  const z=zoneAt(x,y);
  if(!(z<0 || z===routeAllow)) return false;
  // ...and around every piece of furniture, art-accurate, with the same exception.
  const b=blockObjAt(x,y);
  return b<0 || b===allowObj;
}""",
    "furniture is walked around")

rep("""function canStop(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  // No exception here: being allowed IN to use something never makes it somewhere to linger.
  return zoneAt(x,y)<0;
}""",
    """function canStop(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  // No exceptions here: being allowed IN to use something never makes it somewhere to linger,
  // and inside a piece of furniture is not a place at all.
  const sb=allowObj; allowObj=-1;
  const blocked=blockObjAt(x,y)>=0 || blockObjAt(x,y)===-2;
  allowObj=sb;
  return zoneAt(x,y)<0 && !blocked;
}""",
    "and never lingered in")

# the licence: granted per trip alongside the zone licence
rep("""  routeAllow = c.spot ? (zoneAt(raw.x,raw.y)>=0?zoneAt(raw.x,raw.y):zoneAt(c.spot.x,c.spot.y)) : -1;""",
    """  routeAllow = c.spot ? (zoneAt(raw.x,raw.y)>=0?zoneAt(raw.x,raw.y):zoneAt(c.spot.x,c.spot.y)) : -1;
  allowObj = (c.spot && c.spot.oid!=null) ? c.spot.oid : -1;""",
    "the destination object opens")

rep("""       lastKey:keyOf(c), didSomething:false, approach, allowZone:routeAllow,""",
    """       lastKey:keyOf(c), didSomething:false, approach, allowZone:routeAllow,
       allowObjId:allowObj,""",
    "and the trip remembers it")

rep("""      routeAllow = (sim.allowZone==null?-1:sim.allowZone);
      if(sim.allowZone!=null && sim.allowZone>=0 && !sim.spot){""",
    """      routeAllow = (sim.allowZone==null?-1:sim.allowZone);
      allowObj  = (sim.allowObjId==null?-1:sim.allowObjId);
      if(sim.allowZone!=null && sim.allowZone>=0 && !sim.spot){""",
    "held while walking")

rep("""        if(legalWithout){ sim.allowZone=-1; routeAllow=-1 }
      }""",
    """        if(legalWithout){ sim.allowZone=-1; routeAllow=-1 }
      }
      // the object licence ends the same way: standing legal without it
      if(allowObj>=0 && !sim.spot){
        const so=allowObj; allowObj=-1;
        const ok=walkable(sim.x,sim.y);
        allowObj=ok?-1:so;
        if(ok) sim.allowObjId=-1;
      }""",
    "and released on legal ground")

# stepping out of a chair he just used: keep its licence for the descent
rep("""          let lv=zoneAt(sim.x, sim.y);
          if(lv<0) for(let dy=2;dy<=30&&lv<0;dy+=2) lv=zoneAt(sim.x, sim.y+dy);
          if(lv<0 && sim.spot) lv=zoneAt(sim.spot.x, sim.spot.y);
          sim.allowZone=lv;""",
    """          let lv=zoneAt(sim.x, sim.y);
          if(lv<0) for(let dy=2;dy<=30&&lv<0;dy+=2) lv=zoneAt(sim.x, sim.y+dy);
          if(lv<0 && sim.spot) lv=zoneAt(sim.spot.x, sim.spot.y);
          sim.allowZone=lv;
          sim.allowObjId=(sim.spot&&sim.spot.oid!=null)?sim.spot.oid:blockObjAt(sim.x,sim.y+6);""",
    "climbing down keeps the licence")

# ---------------------------------------------------------------- the ghost hit-test catches up
rep("""    const a=anims[i]; if(!a.on || a.locked || !ruleNeeds(a) || i!==asel) continue;""",
    """    const a=anims[i]; if(!a.on || a.locked || isAnywhere(a) || i!==asel) continue;""",
    "pinned and area ghosts are grabbable (console helper)")

rep("""      const a=anims[i]; if(!a || !a.on || a.hidden || a.locked) continue;
      for(const sp of ghostSpots(a)){ const b=ghostBounds(a,sp);
        if(b && q.x>=b.l-2 && q.x<=b.r+2 && q.y>=b.t-2 && q.y<=b.b+2){
          list.push({kind:'ghost', i, a, label:a.name, noDrag:!ruleNeeds(a)}); break } } } }""",
    """      const a=anims[i]; if(!a || !a.on || a.hidden || a.locked) continue;
      for(const sp of ghostSpots(a)){ const b=ghostBounds(a,sp);
        if(b && q.x>=b.l-2 && q.x<=b.r+2 && q.y>=b.t-2 && q.y<=b.b+2){
          // draggable whenever it is PLACED - by needs, by pin, or in an area. Deciding this by
          // `needs` alone predates pins and areas, and made their ghosts unclickable.
          list.push({kind:'ghost', i, a, label:a.name, noDrag:isAnywhere(a)}); break } } } }""",
    "and in the live hit-test")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
