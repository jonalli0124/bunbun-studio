"""Click the same spot again to reach what is underneath.

The capybara is ~58 room-px wide; the armchair he sits in is ~38. He covers it completely. So
whichever one wins the click, the other becomes unreachable - first the chair stole it, then he
did, and either way it reads as "it keeps grabbing both".

Cycling fixes it without a modifier key a child would never find: click once for the top thing,
click the same place again for the next thing down, and round again. The status line names what
you just got, and says whether there is anything under it.
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


rep("""// His actual drawn extent at a spot,""",
    """// Everything under the pointer, nearest-first: ghosts before objects, because he is normally
// standing on the thing he uses.
function whatIsUnder(q){
  const list=[];
  if(!simOn) for(let i=anims.length-1;i>=0;i--){
    const a=anims[i]; if(!a.on) continue;
    for(const sp of ghostSpots(a)){ const b=ghostBounds(a,sp);
      if(b && q.x>=b.l-2 && q.x<=b.r+2 && q.y>=b.t-2 && q.y<=b.b+2){
        list.push({kind:'ghost', i, a, label:a.name}); break } } }
  for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i], {w,h}=sizeOf(o);
    if(q.x>=o.x-w/2&&q.x<=o.x+w/2&&q.y>=o.y-h&&q.y<=o.y)
      list.push({kind:'object', i, o, label:nice(o.object)});
  }
  return list;
}
// Clicking the same place again steps to the next thing down. 6px of slack, because a mouse
// never lands on exactly the same pixel twice.
let lastClick={x:-99,y:-99,n:0};

// His actual drawn extent at a spot,""",
    "everything under the pointer, and the cycle counter")

rep("""  // He is checked BEFORE objects: he is usually standing on one, and if the chair won the tie
  // you could never pick him up. Clicking him also selects his animation, so there is no
  // "select it in the list first" step.
  const gh=pickGhost(q);
  if(gh){
    asel=gh.i; osel=-1; zsel=-1; syncObj(); syncRule();
    gh.a.place=gh.a.place||{dx:0,dy:0};
    drag={anim:gh.a, dx:gh.a.place.dx-q.x, dy:gh.a.place.dy-q.y};
    cv.setPointerCapture(e.pointerId); draw(); return; }
  const hit=pickObj(q);""",
    """  // Pick from the stack under the cursor, advancing if this is a repeat click in the same spot.
  const under=whatIsUnder(q);
  if(under.length){
    const same=Math.abs(q.x-lastClick.x)<6 && Math.abs(q.y-lastClick.y)<6;
    const n=same ? (lastClick.n+1)%under.length : 0;
    lastClick={x:q.x, y:q.y, n};
    const pick=under[n];
    if(pick.kind==='ghost'){
      asel=pick.i; osel=-1; zsel=-1; syncObj(); syncRule();
      pick.a.place=pick.a.place||{dx:0,dy:0};
      drag={anim:pick.a, dx:pick.a.place.dx-q.x, dy:pick.a.place.dy-q.y};
    }else{
      osel=pick.i; asel=-1; zsel=-1; syncObj(); syncRule();
      drag={dx:pick.o.x-q.x, dy:pick.o.y-q.y};
    }
    say(under.length>1
      ? `${pick.label} \\u2014 click again for the ${under[(n+1)%under.length].label} under it`
      : pick.label);
    cv.setPointerCapture(e.pointerId); draw(); return;
  }
  lastClick={x:-99,y:-99,n:0};
  const hit=pickObj(q);""",
    "click again to go deeper")

rep("""cv.addEventListener('pointermove',e=>{ if(drag||zmode) return;
  cv.style.cursor = pickGhost(at(e)) ? 'grab' : (pickObj(at(e))>=0 ? 'move' : 'default') });""",
    """cv.addEventListener('pointermove',e=>{ if(drag||zmode) return;
  const u=whatIsUnder(at(e));
  cv.style.cursor = !u.length ? 'default' : (u[0].kind==='ghost' ? 'grab' : 'move') });""",
    "cursor follows the same rule")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
