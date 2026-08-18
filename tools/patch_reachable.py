"""Stand somewhere he can actually get to, and never cheat when he cannot.

Two faults the audit found, 331 steps inside a keep-out over 2.5 minutes:

1. approachPoint took the NEAREST walkable pixel, which was a 2px sliver between the bottom of the
   keep-out and the bottom of the floor. Nothing can reach a 2px sliver, so the search correctly
   reported "no route" - to a spot that should never have been offered. Standing room now means
   room to stand: a point needs clear space around it, and it must be reachable from where he
   actually is.

2. When the search found no route, the caller fell back to `|| []` - an empty path - which walks
   the straight line. That is precisely the cheat the search existed to prevent. No route now
   means he does not go, and the status line says which thing he cannot reach.
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


rep("""// Somewhere to stand next to a thing. Rings outward from the object until it finds real standing
// room, so a chair wrapped in a keep-out is approached from its edge instead of its middle.
function approachPoint(x,y){
  if(walkable(x,y)) return {x,y};
  for(let r=6;r<=90;r+=4){
    let best=null, bd=1e9;
    for(let k=0;k<24;k++){ const th=k/24*Math.PI*2;
      const px=x+Math.cos(th)*r, py=y+Math.sin(th)*r*0.6;   // squashed: rooms are wider than deep
      if(!walkable(px,py)) continue;
      const d=Math.hypot(px-x,py-y); if(d<bd){bd=d;best={x:px,y:py}} }
    if(best) return best;
  }
  return null;
}""",
    """// Room to STAND, not just a walkable pixel. A 2px gap between a keep-out and the edge of the
// floor passes walkable() and is useless: nothing can reach it and he could not stand there.
function standable(x,y){
  if(!walkable(x,y)) return false;
  return walkable(x-4,y) && walkable(x+4,y) && walkable(x,y-3) && walkable(x,y+3);
}
// Every place he might stand to use a thing, nearest first. The caller picks the first it can
// actually walk to, because "nearest" and "reachable" are not the same question.
function approachCandidates(x,y){
  const out=[];
  if(standable(x,y)) out.push({x,y});
  for(let r=6;r<=110;r+=4){
    const ring=[];
    for(let k=0;k<24;k++){ const th=k/24*Math.PI*2;
      const px=x+Math.cos(th)*r, py=y+Math.sin(th)*r*0.6;   // squashed: rooms are wider than deep
      if(standable(px,py)) ring.push({x:px,y:py}) }
    ring.sort((a,b)=>Math.hypot(a.x-x,a.y-y)-Math.hypot(b.x-x,b.y-y));
    out.push(...ring);
    if(out.length>=14) break;
  }
  return out;
}
const approachPoint=(x,y)=>approachCandidates(x,y)[0]||null;
// Where he can stand AND get to from here.
function reachableApproach(fromX,fromY,x,y){
  for(const c of approachCandidates(x,y)){
    const r=findRoute(fromX,fromY,c.x,c.y);
    if(r!==null) return {spot:c, path:r};
  }
  return null;
}""",
    "standing room must be reachable")

rep("""  const raw = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();
  // He walks to standing room beside it; the offset is where he ends up once he is there.
  const to = c.spot ? (approachPoint(raw.x, raw.y) || raw) : raw;
  const settle = c.spot ? {x:c.spot.x+off.dx, y:c.spot.y+off.dy} : {x:to.x+off.dx, y:to.y+off.dy};""",
    """  const raw = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();
  // Standing room beside it that he can actually walk to, with the route already worked out.
  const reach = c.spot ? reachableApproach(from.x, from.y, raw.x, raw.y) : null;
  if(c.spot && !reach){
    // No way there. Say so rather than walking through the wall, and go and do something else.
    sim={...(sim||{}), phase:'rest', t:0, spot:null, settle:null, didSomething:false,
         x:from.x, y:from.y, a:c.a, lastKey:keyOf(c),
         says:`he cannot get to the ${nice(c.spot.object)} \\u2014 nothing to stand on beside it`};
    return;
  }
  const to = reach ? reach.spot : raw;
  const settle = c.spot ? {x:c.spot.x+off.dx, y:c.spot.y+off.dy} : {x:to.x+off.dx, y:to.y+off.dy};""",
    "no route means he does not go")

rep("""  const path=near?[]:(findRoute(from.x, from.y, to.x, to.y)||[]);""",
    """  // Reuse the route the reachability check already found; a wander re-searches. A null result
  // here would mean "walk straight through", so it is turned into "stay put" instead.
  let path=[];
  if(!near){ const r = reach ? reach.path : findRoute(from.x, from.y, to.x, to.y);
    if(r===null){ sim={...(sim||{}), phase:'rest', t:0, spot:null, settle:null,
      didSomething:false, x:from.x, y:from.y, a:c.a, lastKey:keyOf(c),
      says:'nowhere to go from here'}; return }
    path=r; }""",
    "never fall back to the straight line")

# a wander target has to be reachable too
rep("""  if(sim && sim.didSomething && cands.length){
    sim={...sim, phase:'walk', t:0, spot:null, settle:null, didSomething:false,
         ...(()=>{ const w=someWalkablePoint(); return {tx:w.x, ty:w.y} })(),
         says:'having a wander'};
    sim.walkClip=walkClipFor(sim.a, sim.tx<sim.x?-1:1);
    return;
  }""",
    """  if(sim && sim.didSomething && cands.length){
    // somewhere he can stand AND reach; if the room offers neither, just idle
    let w=null, route=null;
    for(let t=0;t<12;t++){ const c=someWalkablePoint();
      if(!standable(c.x,c.y)) continue;
      const r=findRoute(sim.x,sim.y,c.x,c.y);
      if(r!==null){ w=c; route=r; break } }
    if(!w){ sim={...sim, phase:'rest', t:0, didSomething:false,
      says:'nowhere much to wander to'}; return }
    sim={...sim, phase:'walk', t:0, spot:null, settle:null, didSomething:false,
         tx:(route.length?route[0].x:w.x), ty:(route.length?route[0].y:w.y),
         path:route, goal:{x:w.x,y:w.y}, says:'having a wander'};
    sim.walkClip=walkClipFor(sim.a, sim.tx<sim.x?-1:1);
    return;
  }""",
    "wander only somewhere he can reach")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
