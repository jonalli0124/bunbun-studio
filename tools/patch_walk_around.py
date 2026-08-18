"""Walk AROUND furniture, except the piece you are going to use. And spread his wandering out.

Jon: "he needs to walk around objects and not through for example the bath in the center of the
room hence the keep out zone" - alongside the earlier "he should be able to enter and exit those
areas to do a placed location like sit or bathe".

Those are not in conflict; they are one rule with an exception:

    a keep-out blocks walking THROUGH it
    ...except the one around the thing he is on his way to use, which he may enter

So routing blocks every zone but the destination's own. Wandering and free activities still may
not stop in any of them.

Also implements a shipped rule I had skipped (main.cpp think(): "half his trips are furniture, the
rest a random floor spot AT LEAST 60px AWAY"). Without the minimum distance, wander targets pile
up near wherever he already is, which is what "he seems to favor one side of the room" looks like.
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


rep("""function walkable(x,y){
  const fl=floorPoly();
  return !fl || inPoly(x,y,fl);
}""",
    """// Which keep-out, if any, a point sits in.
function zoneAt(x,y){
  for(let i=0;i<scene.zones.length;i++){ const z=scene.zones[i];
    if(z.kind==='keepout'&&z.pts&&z.pts.length>2&&inPoly(x,y,z.pts)) return i }
  return -1;
}
// The zone he is currently allowed into, because it is the one around the thing he is going to
// use. Set for the length of a route calculation and cleared afterwards.
let routeAllow=-1;
function walkable(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  // He walks AROUND the bath in the middle of the room - that is what its zone is for. The one
  // exception is the zone around whatever he is on his way to use.
  const z=zoneAt(x,y);
  return z<0 || z===routeAllow;
}""",
    "walk around furniture, except what you are using")

# canStop must ignore the exception - entering to use is not the same as loitering
rep("""function canStop(x,y){
  if(!walkable(x,y)) return false;
  return !scene.zones.some(z=>z.kind==='keepout'&&z.pts&&z.pts.length>2&&inPoly(x,y,z.pts));
}""",
    """function canStop(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  // No exception here: being allowed IN to use something never makes it somewhere to linger.
  return zoneAt(x,y)<0;
}""",
    "the exception never licenses loitering")

# the route to a placed activity may enter that thing's zone
rep("""  const reach = c.spot ? reachableApproach(from.x, from.y, raw.x, raw.y, c.spot) : null;""",
    """  // ...and while working out how to get to it, the zone around IT is passable.
  routeAllow = c.spot ? zoneAt(c.spot.x, c.spot.y) : -1;
  const reach = c.spot ? reachableApproach(from.x, from.y, raw.x, raw.y, c.spot) : null;""",
    "the destination's own zone opens")

rep("""  let path=[];
  if(!near){ sim={...(sim||{}), x:from.x, y:from.y};   // off the chair first, onto the boards""",
    """  let path=[];
  if(!near){ sim={...(sim||{}), x:from.x, y:from.y};   // off the chair first, onto the boards""",
    "(no change - anchor)")

rep("""       lastKey:keyOf(c), didSomething:false, approach,""",
    """       lastKey:keyOf(c), didSomething:false, approach, allowZone:routeAllow,""",
    "remember which zone this trip may enter")

# while walking, the same exception has to hold, or the per-frame legality check undoes it
rep("""      if(!walkable(sim.x, sim.y)){
        const fix=nudgeLegal(sim.x, sim.y);""",
    """      routeAllow = (sim.allowZone==null?-1:sim.allowZone);
      if(!walkable(sim.x, sim.y)){
        const fix=nudgeLegal(sim.x, sim.y);""",
    "and holds for every step of the trip")

# a wander has no exception
rep("""    const here = walkable(sim.x,sim.y) ? {x:sim.x,y:sim.y}
               : (sim.approach || nudgeLegal(sim.x,sim.y) || someWalkablePoint());""",
    """    routeAllow = (sim.allowZone==null?-1:sim.allowZone);   // he may still step OUT of one
    const here = walkable(sim.x,sim.y) ? {x:sim.x,y:sim.y}
               : (sim.approach || nudgeLegal(sim.x,sim.y) || someWalkablePoint());""",
    "he can always step back out")

# ---------------------------------------------------------------- spread the wandering
rep("""function someWalkablePoint(){""",
    """// main.cpp think(): the non-furniture half of his trips go to "a random floor spot AT LEAST
// 60px away". Without that minimum, targets cluster around wherever he already is - which reads
// as him favouring one end of the room.
const WANDER_MIN=60;
function someWalkablePoint(awayFrom){""",
    "a minimum distance for a wander")

rep("""  for(let i=0;i<80;i++){ const x=x0+Math.random()*(x1-x0), y=y0+Math.random()*(y1-y0);
    if(stoppable(x,y)) return {x,y} }        // never wander INTO a keep-out""",
    """  // far enough to be a journey, and somewhere he is allowed to linger
  if(awayFrom) for(let i=0;i<120;i++){ const x=x0+Math.random()*(x1-x0), y=y0+Math.random()*(y1-y0);
    if(stoppable(x,y) && Math.hypot(x-awayFrom.x, y-awayFrom.y)>=WANDER_MIN) return {x,y} }
  for(let i=0;i<80;i++){ const x=x0+Math.random()*(x1-x0), y=y0+Math.random()*(y1-y0);
    if(stoppable(x,y)) return {x,y} }        // never wander INTO a keep-out""",
    "far enough to be a journey")

rep("""    for(let t=0;t<12;t++){ const c=someWalkablePoint();""",
    """    for(let t=0;t<12;t++){ const c=someWalkablePoint(here);""",
    "wanders start from where he is")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
