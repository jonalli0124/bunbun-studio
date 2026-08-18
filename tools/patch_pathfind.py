"""Walk round it properly: search the floor, don't guess at corners.

The corner-hopping router could only ever manage a two-leg detour, so a keep-out that nearly spans
the walkable strip - which is what you get when you fence off a chair against the back wall - had
no solution, and it fell back to walking straight through. Measured: 13 steps inside the zone.

Replaced with a breadth-first search over a 4px grid of the walkable area. It finds a way round if
one exists, of any shape and any number of turns, and when there genuinely is no way round it says
so instead of cheating. The route is then smoothed back to as few straight legs as line of sight
allows, so he walks like a pet rather than tracing grid cells.
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


rep("""// A way round, not through. Tries the corners of whatever is in the way, nudged outward, and
// takes the shortest two-leg route that is actually clear.
function routeAround(ax,ay,bx,by){
  if(!pathBlocked(ax,ay,bx,by)) return null;
  const cands=[];
  for(const z of scene.zones){
    if(z.kind!=='keepout'||!z.pts||z.pts.length<3) continue;
    const cx=z.pts.reduce((s,p)=>s+p[0],0)/z.pts.length;
    const cy=z.pts.reduce((s,p)=>s+p[1],0)/z.pts.length;
    for(const [px,py] of z.pts){
      const dx=px-cx, dy=py-cy, L=Math.max(1,Math.hypot(dx,dy));
      for(const pad of [8,16,26]){
        const wx=px+dx/L*pad, wy=py+dy/L*pad;
        if(walkable(wx,wy)) cands.push({x:wx,y:wy});
      }
    }
  }
  let best=null, bd=1e9;
  for(const c of cands){
    if(pathBlocked(ax,ay,c.x,c.y) || pathBlocked(c.x,c.y,bx,by)) continue;
    const d=Math.hypot(c.x-ax,c.y-ay)+Math.hypot(bx-c.x,by-c.y);
    if(d<bd){ bd=d; best=c }
  }
  return best;
}""",
    """// A route over the walkable area, found rather than guessed. Breadth-first across a 4px grid:
// it will go up and over a keep-out that spans nearly the whole strip, which corner-hopping
// could not, and it returns nothing when there is honestly no way through.
const CELL=4;
function gridBounds(){
  const fl=floorPoly();
  if(fl){ const xs=fl.map(p=>p[0]), ys=fl.map(p=>p[1]);
    return {x0:Math.min(...xs)-CELL, y0:Math.min(...ys)-CELL,
            x1:Math.max(...xs)+CELL, y1:Math.max(...ys)+CELL} }
  return {x0:0, y0:GAME.floor-40, x1:GAME.w, y1:GAME.h};
}
function findRoute(ax,ay,bx,by){
  if(!pathBlocked(ax,ay,bx,by)) return [];          // clear line, no waypoints needed
  const B=gridBounds();
  const W=Math.max(1,Math.ceil((B.x1-B.x0)/CELL)), H=Math.max(1,Math.ceil((B.y1-B.y0)/CELL));
  const cx=v=>Math.min(W-1,Math.max(0,Math.round((v-B.x0)/CELL)));
  const cy=v=>Math.min(H-1,Math.max(0,Math.round((v-B.y0)/CELL)));
  const px=i=>B.x0+i*CELL, py=j=>B.y0+j*CELL;
  const ok=new Uint8Array(W*H);
  for(let j=0;j<H;j++)for(let i=0;i<W;i++) ok[j*W+i]=walkable(px(i),py(j))?1:0;
  const si=cx(ax), sj=cy(ay), gi=cx(bx), gj=cy(by);
  ok[sj*W+si]=1; ok[gj*W+gi]=1;                     // never rule out where he is or is going
  const prev=new Int32Array(W*H).fill(-1);
  const seen=new Uint8Array(W*H);
  const q=[sj*W+si]; seen[sj*W+si]=1;
  const goal=gj*W+gi;
  let found=false;
  for(let h=0; h<q.length && !found; h++){
    const c=q[h], ci=c%W, cj=(c-ci)/W;
    for(let dj=-1;dj<=1;dj++)for(let di=-1;di<=1;di++){
      if(!di&&!dj) continue;
      const ni=ci+di, nj=cj+dj; if(ni<0||nj<0||ni>=W||nj>=H) continue;
      const n=nj*W+ni; if(seen[n]||!ok[n]) continue;
      // no cutting a diagonal past a corner
      if(di&&dj&&(!ok[cj*W+ni]||!ok[nj*W+ci])) continue;
      seen[n]=1; prev[n]=c; q.push(n);
      if(n===goal){ found=true; break }
    }
  }
  if(!found) return null;                            // genuinely no way round
  const cells=[]; for(let c=goal;c!==-1;c=prev[c]) cells.push(c);
  cells.reverse();
  const pts=cells.map(c=>{const i=c%W; return {x:px(i), y:py((c-i)/W)}});
  // smooth: keep only the turns line of sight actually needs
  const out=[]; let cur={x:ax,y:ay}, k=0;
  while(k<pts.length-1){
    let far=k;
    for(let m=pts.length-1;m>k;m--){ if(!pathBlocked(cur.x,cur.y,pts[m].x,pts[m].y)){ far=m; break } }
    if(far===k) far=k+1;
    cur=pts[far]; k=far;
    if(k<pts.length-1) out.push({x:cur.x,y:cur.y});
  }
  return out;
}""",
    "breadth-first route over the floor")

# ---- the sim follows a list of waypoints now, not a single corner
rep("""  const via=near?null:routeAround(from.x, from.y, to.x, to.y);
  sim={...(sim||{}), x:near?settle.x:from.x, y:near?settle.y:from.y,
       tx:via?via.x:to.x, ty:via?via.y:to.y, via, goal:{x:to.x,y:to.y},""",
    """  const path=near?[]:(findRoute(from.x, from.y, to.x, to.y)||[]);
  const first=path.length?path[0]:to;
  sim={...(sim||{}), x:near?settle.x:from.x, y:near?settle.y:from.y,
       tx:first.x, ty:first.y, path, goal:{x:to.x,y:to.y},""",
    "carry the whole route")

rep("""    if(d<=step && sim.via){                      // reached the corner; now head for the thing
      sim.x=sim.via.x; sim.y=sim.via.y; sim.via=null;
      sim.tx=sim.goal.x; sim.ty=sim.goal.y; return }""",
    """    if(d<=step && sim.path && sim.path.length){  // reached a turn; take the next leg
      sim.x=sim.tx; sim.y=sim.ty; sim.path.shift();
      const nx=sim.path.length?sim.path[0]:sim.goal;
      sim.tx=nx.x; sim.ty=nx.y; return }""",
    "walk the route leg by leg")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
