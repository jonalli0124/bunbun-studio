"""Direct him: a button per animation that makes him go and do THAT, now.

Jon: "i want to watch him sleep or eat or any of the other action / emotion things". While
Act it out runs, every live animation gets a "make him" button - click it and whatever he is
doing, he heads off to do that instead: same walk, same front entry, same rules, just your
choice instead of his. If the preview is off, the button turns it on.

(The full game CHROME - pause chip, tabs, stat cells - is still blocked on art: the ui/*
sprites are not in the asset pack.)
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


rep("""function chooseNext(){
  const cands=candidates();
  if(!cands.length){ sim=null; return }""",
    """let forcePick=null;      // {a, spot} - the next choice is YOURS, not the dice's
function chooseNext(){
  const cands=candidates();
  if(!cands.length){ sim=null; return }
  if(forcePick){
    const f=forcePick; forcePick=null;
    // the same trip as any other - walk, front entry, licences - just this destination
    return goTo(f);
  }""",
    "a forced choice wins the next pick")

# chooseNext's tail becomes goTo(c) so the forced path and the dice share every line
rep("""  // and prefer something other than the thing he just did, when there is a choice
  const fresh=cands.filter(c=>!(sim&&sim.lastKey===keyOf(c)));
  const pool=fresh.length?fresh:cands;
  const c=pool[Math.floor(Math.random()*pool.length)];""",
    """  // and prefer something other than the thing he just did, when there is a choice
  const fresh=cands.filter(c=>!(sim&&sim.lastKey===keyOf(c)));
  const pool=fresh.length?fresh:cands;
  const c=pool[Math.floor(Math.random()*pool.length)];
  return goTo(c);
}
function goTo(c){""",
    "one trip-builder for both")

rep("""      <button id="marksBtn" title="the red spot under the ghost and the purple ring under things that afford an action">Circles: on</button>""",
    """      <button id="marksBtn" title="the red spot under the ghost and the purple ring under things that afford an action">Circles: on</button>
    </div>
    <div class="row" id="forceRow" style="margin-top:8px;flex-wrap:wrap;gap:5px"></div>
    <div class="row" style="display:none">""",
    "a row for the direction buttons")

rep("""  ensureAcodes();
  $('anims').innerHTML = anims.length ? anims.map((a,i)=>{""",
    """  ensureAcodes();
  // DIRECT HIM: one button per live animation. Click - he stops deliberating and goes.
  $('forceRow').innerHTML = anims.filter(a=>a.on).length
    ? '<span class="hint" style="flex:0 0 auto;align-self:center">make him:</span>'+
      anims.map((a,i)=>a.on?`<button class="mini" data-force="${i}">${a.name.length>18
        ?a.name.slice(0,17)+'\\u2026':a.name}</button>`:'').join('')
    : '';
  $('forceRow').querySelectorAll('[data-force]').forEach(b=>b.onclick=async()=>{
    const a=anims[+b.dataset.force];
    const p=placesFor(a);
    if(p.kind==='needs'&&!p.spots.length)
      return say(`nothing in the room lets him do "${a.name}" \\u2014 give something the `+
                 `affordance, pin an item, or bind an area`,true);
    // nearest place wins when there are several - you asked to WATCH it, not to wait
    let spot=null;
    if(p.kind==='needs'){ const fx=sim?sim.x:GAME.w/2, fy=sim?sim.y:GAME.floor;
      spot=p.spots.slice().sort((q,r)=>Math.hypot(q.x-fx,q.y-fy)-Math.hypot(r.x-fx,r.y-fy))[0]; }
    forcePick={a, spot};
    if(!simOn){ await $('preview').onclick() }
    if(sim){ sim.phase='rest'; sim.t=1e9; sim.waitFor=0 }     // the next tick departs
    say(`going: "${a.name}"${spot?(' at the '+nice(spot.object)):''}`);
  });
  $('anims').innerHTML = anims.length ? anims.map((a,i)=>{""",
    "the buttons, wired")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
