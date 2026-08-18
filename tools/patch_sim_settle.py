"""Stop him spinning, and let him sit in FRONT of the chair.

"he is stuck spinning around" - with one chair and one animation he finished, chose the only
candidate again, and set off on a 12px walk back to the point he was already standing on. Over a
distance that short the sign of dx flips every tick, and facing is a CLIP swap here (east and west
are separate art), so he flickered between the two walk clips. That is the spin.

Three fixes:
  - already there? Don't walk. Settle straight into the animation.
  - having just done a thing, WANDER somewhere first, and prefer a spot he did not just use.
    A pet that ping-pongs between two states is not ambient, it is a loop.
  - facing is sticky: it only changes on a move big enough to mean something.

"it ended up behind the chair" - his depth defaulted to his feet, and sitting puts his feet ABOVE
the chair's base, so he sorted behind it. An animation bound to an object now draws in front of
that object by default, which is what sitting on something looks like, with a control to put him
behind for the cases where that is right (standing behind a counter).
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


# ---------------------------------------------------------------- depth
rep("""  const charZ=(()=>{
    if(simOn&&sim){ const st=sim.a&&sim.a.stage;
      return (sim.phase!=='walk' && st && st.z!=null) ? st.z : sim.y }
    const a=anims[asel]; const st=a&&a.stage;
    return (st&&st.z!=null) ? st.z : null })();""",
    """  // Sitting on a chair puts his feet ABOVE its base, so sorting him by his feet buries him
  // behind it. An animation tied to an object belongs in FRONT of that object unless you say
  // otherwise; travelling, his feet are the honest answer.
  const depthWith=(a,spot,fallback)=>{
    if(a && a.depth==='behind') return (spot? (spot.z==null?spot.y:spot.z) : fallback)-1;
    if(a && a.depth==='front')  return (spot? (spot.z==null?spot.y:spot.z) : fallback)+1;
    if(a && a.stage && a.stage.z!=null) return a.stage.z;
    if(spot) return (spot.z==null?spot.y:spot.z)+1;
    return fallback;
  };
  const charZ=(()=>{
    if(simOn&&sim){
      if(sim.phase==='walk') return sim.y;
      return depthWith(sim.a, sim.spot, sim.y) }
    const a=anims[asel]; if(!a) return null;
    const p=placesFor(a);
    return depthWith(a, p.kind==='needs'?p.spots[0]:null, null) })();""",
    "sit in front of what you sit on")

rep("""          <div class="hint" id="apNote">drag him in the room to place him</div>""",
    """          <div class="hint" id="apNote">drag him in the room to place him</div>
          <div class="row" style="margin-top:6px">
            <button class="mini" id="apFront">in front of it</button>
            <button class="mini" id="apBehind">behind it</button>
          </div>""",
    "a depth choice for the animation")

rep("""$('apReset').onclick=()=>{""",
    """$('apFront').onclick=()=>{ const a=anims[asel]; if(!a)return;
  a.depth=a.depth==='front'?null:'front'; syncRule(); draw() };
$('apBehind').onclick=()=>{ const a=anims[asel]; if(!a)return;
  a.depth=a.depth==='behind'?null:'behind'; syncRule(); draw() };
$('apReset').onclick=()=>{""",
    "wire the depth choice")

rep("""  { const pl=a.place||{dx:0,dy:0};""",
    """  $('apFront').classList.toggle('pri', a.depth==='front');
  $('apBehind').classList.toggle('pri', a.depth==='behind');
  { const pl=a.place||{dx:0,dy:0};""",
    "show which depth is chosen")

# ---------------------------------------------------------------- the loop
rep("""function chooseNext(){
  const cands=candidates();
  if(!cands.length){ sim=null; return }
  const c=cands[Math.floor(Math.random()*cands.length)];""",
    """function chooseNext(){
  const cands=candidates();
  if(!cands.length){ sim=null; return }
  // Having just used something, go and be somewhere else for a bit. Without this he finishes a
  // sit, re-picks the only chair, and walks 12px back onto the spot he is already on - forever.
  if(sim && sim.didSomething && cands.length){
    sim={...sim, phase:'walk', t:0, spot:null, settle:null, didSomething:false,
         ...(()=>{ const w=someWalkablePoint(); return {tx:w.x, ty:w.y} })(),
         says:'having a wander'};
    sim.walkClip=walkClipFor(sim.a, sim.tx<sim.x?-1:1);
    return;
  }
  // and prefer something other than the thing he just did, when there is a choice
  const fresh=cands.filter(c=>!(sim&&sim.lastKey===keyOf(c)));
  const pool=fresh.length?fresh:cands;
  const c=pool[Math.floor(Math.random()*pool.length)];""",
    "wander between things")

rep("""const WALK_PXS=16;""",
    """const keyOf=c=>c.a.name+'@'+(c.spot?Math.round(c.spot.x)+','+Math.round(c.spot.y):'anywhere');
const WALK_PXS=16;""",
    "how a choice is identified")

rep("""  sim={...(sim||{}), x:from.x, y:from.y, tx:to.x, ty:to.y,
       a:c.a, spot:c.spot, phase:'walk', t:0, plays:0,""",
    """  // Already standing where this happens? Then it happens. Walking a handful of pixels to a
  // place you are already at is what the spinning looked like.
  const near=Math.hypot(settle.x-from.x, settle.y-from.y)<6;
  sim={...(sim||{}), x:near?settle.x:from.x, y:near?settle.y:from.y, tx:to.x, ty:to.y,
       a:c.a, spot:c.spot, phase:near?'play':'walk', t:0, plays:0,
       lastKey:keyOf(c), didSomething:false,""",
    "no walk when he is already there")

rep("""    if(d<=step){ sim.x=(sim.settle?sim.settle.x:sim.tx); sim.y=(sim.settle?sim.settle.y:sim.ty);
      sim.phase='play'; sim.t=0;""",
    """    if(d<=step){ sim.x=(sim.settle?sim.settle.x:sim.tx); sim.y=(sim.settle?sim.settle.y:sim.ty);
      sim.phase = sim.spot ? 'play' : 'rest';     // a wander ends in a pause, not an action
      sim.t=0;""",
    "a wander ends in a pause")

rep("""    else { sim.x+=dx/d*step; sim.y+=dy/d*step;
      if(Math.abs(dx)>0.5) sim.face=dx<0?-1:1;
      sim.walkClip=walkClipFor(sim.a, sim.face||1) }""",
    """    else { sim.x+=dx/d*step; sim.y+=dy/d*step;
      // Sticky facing. East and west are separate CLIPS here, so a dx that dithers around zero
      // swaps the art every tick and reads as spinning on the spot.
      if(Math.abs(dx)>3) sim.face=dx<0?-1:1;
      sim.walkClip=walkClipFor(sim.a, sim.face||1) }""",
    "facing stops flickering")

rep("""    const loops=Math.floor(sim.t*(sim.a.fps||7)/seq);
    if(loops>=2){ sim.phase='rest'; sim.t=0; sim.says='looking for something to do' }""",
    """    const loops=Math.floor(sim.t*(sim.a.fps||7)/seq);
    if(loops>=2){ sim.phase='rest'; sim.t=0; sim.didSomething=true;
      sim.says='looking for something to do' }""",
    "remember he just did something")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
