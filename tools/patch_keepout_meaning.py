"""A keep-out says "do not LOITER here", not "do not walk here".

Jon, settling what the zone actually means:

    "the idea for the keep out is for him to not do anywhere he walks activities in those spots"
    "he should be able to enter and exit those areas to do a placed location like sit or bathe"

which is the same distinction main.cpp:1807 already draws about the chair - "The zone says 'not ON
the chair'; it never said 'not in front of it', and those are different sentences."

I had built it as a wall. That forced routing round it, split his 28px floor in two when a keep-out
crossed it, and made the chair inside its own zone unreachable. Three separate bugs, all from
reading the zone as geometry rather than as a rule about behaviour.

    walking     bounded by the WALKABLE FLOOR alone. A keep-out never blocks a step; he has to
                cross one to sit in the chair standing inside it.
    stopping    an "anywhere he walks" activity, and a wander target, may not be inside one.
    a placed    activity at an object may be anywhere at all - that object is the whole reason
    activity    the zone is drawn around it.
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


rep("""// A keep-out blocks its whole COLUMN down to its front edge, not just its outline.
//
// main.cpp:1815 clearOfBlocks() resolves a point inside a block by setting y to that block's
// yFront - the front edge of the furniture. The pet is never put behind a block; he is pushed
// out in front of it. Testing only the outline let him walk in the strip BETWEEN a keep-out and
// the back of the floor, which is how he ended up behind the chair, and it forced a fat
// footprint test to prevent that - which in turn cut Jon's 28px floor in two, so nothing could
// be reached. This rule does both jobs: behind is not walkable at all, and the strip in FRONT
// stays open at its full depth.
function keepOutFront(x,y){
  for(const z of scene.zones){
    if(z.kind!=='keepout'||!z.pts||z.pts.length<3) continue;
    const xs=z.pts.map(p=>p[0]), ys=z.pts.map(p=>p[1]);
    if(x<Math.min(...xs)||x>Math.max(...xs)) continue;
    if(y<=Math.max(...ys)) return true;      // level with it, or behind it
  }
  return false;
}
function walkable(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  return !keepOutFront(x,y);
}""",
    """// WHERE HE MAY WALK is the walkable floor, and only that. A keep-out is not a wall - he has to
// walk into one to sit in a chair that stands inside it.
function walkable(x,y){
  const fl=floorPoly();
  return !fl || inPoly(x,y,fl);
}
// WHERE HE MAY STOP OF HIS OWN ACCORD. This is what a keep-out is for: no wandering to it, and
// no doing an "anywhere he walks" activity there. Going there to use the thing inside it is
// exactly what it is for, so that is decided elsewhere.
function canStop(x,y){
  if(!walkable(x,y)) return false;
  return !scene.zones.some(z=>z.kind==='keepout'&&z.pts&&z.pts.length>2&&inPoly(x,y,z.pts));
}""",
    "a keep-out stops loitering, not walking")

# standing room is about the floor; stopping room adds the keep-out rule
rep("""const FOOT_W=5, FOOT_H=3;
function standable(x,y){
  if(!walkable(x,y)) return false;
  return walkable(x-FOOT_W,y) && walkable(x+FOOT_W,y)
      && walkable(x,y-FOOT_H) && walkable(x,y+FOOT_H);
}""",
    """const FOOT_W=5, FOOT_H=3;
function standable(x,y){
  if(!walkable(x,y)) return false;
  return walkable(x-FOOT_W,y) && walkable(x+FOOT_W,y)
      && walkable(x,y-FOOT_H) && walkable(x,y+FOOT_H);
}
// somewhere he would choose to stop: room to stand, and not inside a keep-out
const stoppable=(x,y)=>standable(x,y) && canStop(x,y);""",
    "somewhere he would choose to stop")

# wander targets must be places he may stop
rep("""  for(let i=0;i<80;i++){ const x=x0+Math.random()*(x1-x0), y=y0+Math.random()*(y1-y0);
    if(standable(x,y)) return {x,y} }""",
    """  for(let i=0;i<80;i++){ const x=x0+Math.random()*(x1-x0), y=y0+Math.random()*(y1-y0);
    if(stoppable(x,y)) return {x,y} }        // never wander INTO a keep-out
  for(let i=0;i<80;i++){ const x=x0+Math.random()*(x1-x0), y=y0+Math.random()*(y1-y0);
    if(standable(x,y)) return {x,y} }""",
    "he does not wander into a keep-out")

rep("""    let w=null, route=null;
    for(let t=0;t<12;t++){ const c=someWalkablePoint();
      if(!standable(c.x,c.y)) continue;""",
    """    let w=null, route=null;
    for(let t=0;t<12;t++){ const c=someWalkablePoint();
      if(!stoppable(c.x,c.y)) continue;      // a wander ends somewhere he is allowed to linger""",
    "wanders end outside the zones")

# an "anywhere" activity may not happen inside one either
rep("""  const raw = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();""",
    """  // An "anywhere he walks" activity has to find a spot he is allowed to stop in; a placed one
  // goes to its object, keep-out or not.
  let raw = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();
  if(!c.spot){ for(let t=0;t<12 && !stoppable(raw.x,raw.y);t++) raw=someWalkablePoint() }""",
    "free activities keep out of the zones")

# routing no longer has to dodge them
rep("""  for(let j=0;j<H;j++)for(let i=0;i<W;i++) ok[j*W+i]=standable(px(i),py(j))?1:0;""",
    """  // Routing cares about the floor only. Walking THROUGH a keep-out is allowed and often
  // necessary - the chair he is going to sit in stands inside one.
  for(let j=0;j<H;j++)for(let i=0;i<W;i++) ok[j*W+i]=standable(px(i),py(j))?1:0;""",
    "note on routing")

# and the ghost for an anywhere animation should sit somewhere he could stop
rep("""    if(fl){ const xs=fl.pts.map(q=>q[0]), ys=fl.pts.map(q=>q[1]);
      return [{x:(Math.min(...xs)+Math.max(...xs))/2, y:(Math.min(...ys)+Math.max(...ys))/2}] }
    return [{x:GAME.w/2, y:GAME.floor}];""",
    """    if(fl){ const xs=fl.pts.map(q=>q[0]), ys=fl.pts.map(q=>q[1]);
      const mid={x:(Math.min(...xs)+Math.max(...xs))/2, y:(Math.min(...ys)+Math.max(...ys))/2};
      // show it somewhere it could actually happen, not in the middle of a keep-out
      return [stoppable(mid.x,mid.y) ? mid : (someWalkablePoint()||mid)];
    }
    return [{x:GAME.w/2, y:GAME.floor}];""",
    "the ghost stands where it could happen")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
