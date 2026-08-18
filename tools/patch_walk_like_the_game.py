"""Port the walk from the firmware instead of inventing it, and honour the depth I was told.

Read components/bunbun/main.cpp think()/keepLegal() rather than guessing. What was wrong:

  speed        42 px/s in the game (main.cpp:2549). Mine was 16, which I made up.
  arrival      d < 4 (main.cpp:2533). Mine closed on the step size, so he crept.
  waiting      18-42s between journeys (main.cpp:2538). Mine was 2s, which is pacing, not a
               lofi pet. The charter is that he is mostly still; a PACE control scales only the
               clocks so the preview stays watchable, exactly as the standing rule requires -
               the constants themselves are untouched.
  facing       the game picks by fabsf(dx) > fabsf(dy) (main.cpp:2554). Mine flipped on the sign
               of dx alone, so a mostly-vertical step swapped east and west art every frame.
               That is the twirling.
  legality     clearOfBlocks() runs on EVERY WALKED FRAME and keepLegal() once a frame. Mine
               only checked the destination. main.cpp:2019 has the note: "nothing WALKED through
               a zone, but bunbun STOOD in one for 13,667 frames. Arriving is not walking."

And the depth: an animation told to be in front of the thing it uses stays in front of it for as
long as he is there - sitting, and still sitting afterwards. Only genuine travel sorts by feet.
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


# ---------------------------------------------------------------- the real constants
rep("""const WALK_PXS=16;              // main.cpp wander speed
const REST_S=2.0;               // long enough for the idle to read as a pause, not a hitch""",
    """// All four ported from main.cpp think(). Do not tune these - tune PACE.
const WALK_PXS=42;              // main.cpp:2549  g_fx += dx/d * 42.0f * dt
const ARRIVE_PX=4;              // main.cpp:2533  if (d < 4)
const WAIT_MIN=18, WAIT_MAX=42; // main.cpp:2538  18-42s between journeys
// He IS that still - that is the charter. This scales only the CLOCKS so a preview is
// watchable; the rules and the constants above are untouched.
let PACE=8;
const waitSecs=()=>(WAIT_MIN+Math.random()*(WAIT_MAX-WAIT_MIN))/PACE;""",
    "the firmware's own numbers")

rep("""      <label style="margin-top:10px">he travels at""",
    """      <label style="margin-top:10px">preview runs <span id="pcl">8</span>&times; real time
        <span class="hint">&mdash; the device waits 18-42s between journeys</span></label>
      <input type="range" id="pc" min="1" max="20" step="1" value="8" style="width:100%">

      <label style="margin-top:10px">he travels at""",
    "a pace control, clearly labelled")

rep("""$('ts').oninput=e=>{ travelScale=+e.target.value/100;""",
    """$('pc').oninput=e=>{ PACE=+e.target.value; $('pcl').textContent=PACE; };
$('ts').oninput=e=>{ travelScale=+e.target.value/100;""",
    "wire the pace")

# ---------------------------------------------------------------- the walk itself
rep("""  if(sim.phase==='walk'){
    const dx=sim.tx-sim.x, dy=sim.ty-sim.y, d=Math.hypot(dx,dy);
    const step=WALK_PXS*dt;""",
    """  if(sim.phase==='walk'){
    const dx=sim.tx-sim.x, dy=sim.ty-sim.y, d=Math.hypot(dx,dy);
    const step=WALK_PXS*dt*PACE;""",
    "walk at the game's speed")

rep("""    if(d<=step && sim.path && sim.path.length){  // reached a turn; take the next leg""",
    """    if(d<ARRIVE_PX && sim.path && sim.path.length){  // reached a turn; take the next leg""",
    "arrive like the game does, at the turns")

rep("""    if(d<=step){ sim.x=(sim.settle?sim.settle.x:sim.tx); sim.y=(sim.settle?sim.settle.y:sim.ty);""",
    """    if(d<Math.max(ARRIVE_PX,step)){ sim.x=(sim.settle?sim.settle.x:sim.tx); sim.y=(sim.settle?sim.settle.y:sim.ty);""",
    "and at the destination")

rep("""    else { sim.x+=dx/d*step; sim.y+=dy/d*step;
      // Sticky facing. East and west are separate CLIPS here, so a dx that dithers around zero
      // swaps the art every tick and reads as spinning on the spot.
      if(Math.abs(dx)>3) sim.face=dx<0?-1:1;
      sim.walkClip=walkClipFor(sim.a, sim.face||1) }""",
    """    else {
      sim.x+=dx/d*step; sim.y+=dy/d*step;
      // main.cpp:2554 picks the clip by whether the trip is mostly horizontal:
      //   bool horiz = fabsf(dx) > fabsf(dy);
      // Facing off the sign of dx alone means a mostly-VERTICAL step flips east/west every
      // frame, which is the twirling. Vertical movement keeps whatever he was already facing.
      if(Math.abs(dx)>Math.abs(dy) && Math.abs(dx)>1) sim.face=dx<0?-1:1;
      sim.walkClip=walkClipFor(sim.a, sim.face||1);
      // main.cpp:2551 clearOfBlocks(&nx,&ny) on EVERY walked frame, and keepLegal() once a
      // frame after that. Checking only the destination is what let him stand in a zone for
      // 13,667 frames. Here, walking into anything illegal is undone immediately.
      if(!walkable(sim.x, sim.y)){
        const fix=nudgeLegal(sim.x, sim.y);
        if(fix){ sim.x=fix.x; sim.y=fix.y }
        else { sim.phase='rest'; sim.t=0; sim.says='that way is blocked' }
      }
    }""",
    "the game's facing rule, and legality every frame")

rep("""function someWalkablePoint(){""",
    """// keepLegal()'s job: put him back on legal ground, nearest first. Called after a step, never
// as a substitute for routing - the route is what stops him needing this.
function nudgeLegal(x,y){
  for(let r=2;r<=20;r+=2)
    for(let k=0;k<16;k++){ const th=k/16*Math.PI*2;
      const px=x+Math.cos(th)*r, py=y+Math.sin(th)*r*0.7;
      if(walkable(px,py)) return {x:px,y:py} }
  return null;
}

function someWalkablePoint(){""",
    "put him back on legal ground")

# ---------------------------------------------------------------- the long stillness
rep("""  } else if(sim.t>REST_S) chooseNext();   // idle, then walk""",
    """  } else if(sim.t > (sim.waitFor||waitSecs())) chooseNext();   // idle, then walk""",
    "the game's 18-42s between journeys")

rep("""    if(loops>=2){ sim.phase='rest'; sim.t=0; sim.didSomething=true;
      sim.says='looking for something to do' }""",
    """    if(loops>=2){ sim.phase='rest'; sim.t=0; sim.didSomething=true;
      sim.waitFor=waitSecs();          // main.cpp:2538, scaled only by PACE
      sim.says='looking for something to do' }""",
    "arm the wait on arrival")

# ---------------------------------------------------------------- depth, as told
rep("""    if(simOn&&sim){
      if(sim.phase==='walk') return sim.y;
      return depthWith(sim.a, sim.spot, sim.y) }""",
    """    if(simOn&&sim){
      // Travelling, his feet decide - that is how the room sorts. But once he is AT a thing,
      // whatever the animation was told stands, and it stays standing while he is still there
      // afterwards. "In front of the chair" must not stop being true the moment he finishes.
      if(sim.phase==='walk') return sim.y;
      return depthWith(sim.a, sim.spot, sim.y) }""",
    "depth holds while he is there")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
