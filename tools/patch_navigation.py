"""Make a keep-out mean what you drew it to mean.

You draw a keep-out round the chair so he does not walk THROUGH the chair. It was being read as
"this place does not exist": his destination was the chair's own point, which now sat inside the
zone, and he walked straight across it anyway to reach rim points on the far side - back and
forth over the same patch, which is the circling.

A keep-out governs WALKING. It says nothing about whether he may use the thing inside it.

  approach   he walks to the nearest standing room BESIDE the object, not to the object itself
  route      if the straight line crosses a keep-out he goes round it, via its corners
  settle     having arrived, he sits where you placed him - inside the zone is fine, that is
             the whole point of sitting on a chair
  say so     if there is genuinely nowhere to stand near it, that is a sentence, not a loop
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


rep("""function someWalkablePoint(){""",
    """// Does the straight line from a to b cross anything he must not walk through? Sampled rather
// than solved: at 3px steps nothing he could squeeze through is wide enough to matter.
function pathBlocked(ax,ay,bx,by){
  const d=Math.hypot(bx-ax,by-ay), n=Math.max(2,Math.ceil(d/3));
  for(let i=1;i<n;i++){ const t=i/n;
    if(!walkable(ax+(bx-ax)*t, ay+(by-ay)*t)) return true }
  return false;
}
// Somewhere to stand next to a thing. Rings outward from the object until it finds real standing
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
}
// A way round, not through. Tries the corners of whatever is in the way, nudged outward, and
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
}

function someWalkablePoint(){""",
    "navigation: can he get there, and how")

# ---- destinations become approach points
rep("""  const off=c.a.place||{dx:0,dy:0};
  const to = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();
  const settle = c.spot ? {x:c.spot.x+off.dx, y:c.spot.y+off.dy} : {x:to.x+off.dx, y:to.y+off.dy};""",
    """  const off=c.a.place||{dx:0,dy:0};
  const raw = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();
  // He walks to standing room beside it; the offset is where he ends up once he is there.
  const to = c.spot ? (approachPoint(raw.x, raw.y) || raw) : raw;
  const settle = c.spot ? {x:c.spot.x+off.dx, y:c.spot.y+off.dy} : {x:to.x+off.dx, y:to.y+off.dy};""",
    "walk to standing room beside it")

rep("""  const near=Math.hypot(settle.x-from.x, settle.y-from.y)<6;
  sim={...(sim||{}), x:near?settle.x:from.x, y:near?settle.y:from.y, tx:to.x, ty:to.y,""",
    """  const near=Math.hypot(settle.x-from.x, settle.y-from.y)<6;
  // and a way round anything between him and it
  const via=near?null:routeAround(from.x, from.y, to.x, to.y);
  sim={...(sim||{}), x:near?settle.x:from.x, y:near?settle.y:from.y,
       tx:via?via.x:to.x, ty:via?via.y:to.y, via, goal:{x:to.x,y:to.y},""",
    "route around what is in the way")

# ---- consume the waypoint, then carry on to the goal
rep("""    if(d<=step){ sim.x=(sim.settle?sim.settle.x:sim.tx); sim.y=(sim.settle?sim.settle.y:sim.ty);
      sim.phase = sim.spot ? 'play' : 'rest';     // a wander ends in a pause, not an action
      sim.t=0;""",
    """    if(d<=step && sim.via){                      // reached the corner; now head for the thing
      sim.x=sim.via.x; sim.y=sim.via.y; sim.via=null;
      sim.tx=sim.goal.x; sim.ty=sim.goal.y; return }
    if(d<=step){ sim.x=(sim.settle?sim.settle.x:sim.tx); sim.y=(sim.settle?sim.settle.y:sim.ty);
      sim.phase = sim.spot ? 'play' : 'rest';     // a wander ends in a pause, not an action
      sim.t=0;""",
    "walk the second leg")

# ---- and say it plainly when a thing cannot be reached at all
rep("""function candidates(){
  const out=[];
  for(const a of anims){ if(!a.on) continue;
    const p=placesFor(a);
    if(p.kind==='anywhere') out.push({a, spot:null});
    else for(const o of p.spots) out.push({a, spot:o});
  }
  return out;
}""",
    """function candidates(){
  const out=[];
  for(const a of anims){ if(!a.on) continue;
    const p=placesFor(a);
    if(p.kind==='anywhere') out.push({a, spot:null});
    // Something with no standing room anywhere near it can never be used, and looping forever
    // trying is worse than saying so.
    else for(const o of p.spots) if(approachPoint(o.x,o.y)) out.push({a, spot:o});
  }
  return out;
}
// what got ruled out, for the message
function unreachable(){
  const out=[];
  for(const a of anims){ if(!a.on) continue;
    const p=placesFor(a);
    if(p.kind==='needs') for(const o of p.spots)
      if(!approachPoint(o.x,o.y)) out.push(nice(o.object));
  }
  return out;
}""",
    "skip what he cannot reach")

rep("""  if(simOn){ const n=candidates().length;
    say(n ? `watching \\u2014 ${n} thing${n===1?'':'s'} he could choose`
          : 'nothing to watch \\u2014 turn an animation on, and mark something it can use');""",
    """  if(simOn){ const n=candidates().length, bad=unreachable();
    say(n ? `watching \\u2014 ${n} thing${n===1?'':'s'} he could choose`+
            (bad.length?`; he cannot reach the ${bad.join(', ')} \\u2014 the keep-out leaves nowhere to stand beside it`:'')
          : (bad.length
             ? `he cannot reach the ${bad.join(', ')} \\u2014 the keep-out leaves nowhere to stand beside it`
             : 'nothing to watch \\u2014 turn an animation on, and mark something it can use'));""",
    "name what he cannot reach")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
