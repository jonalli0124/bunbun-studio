"""Bring across every scene-side control the editor had that the assembler lacked.

Diffed the two tools' controls rather than trusting memory. Everything animation-side stays in the
editor (that is the split). What was missing here and belongs here:

  fade                object opacity
  brightness/spread   the lamp's own power and reach - geomFor already read lampPower/lampSize,
  bulb height         and bulbY, so the maths was here all along with no way to touch it
  depth buttons       front/back nudges beside the depth slider
  arrow-key nudge     one pixel at a time, which a slider cannot do
  lighting on/off     the editor's master toggle, for judging a room flat
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


rep("""        <label style="margin-top:8px">what can be done here</label>
        <div id="objCan" class="row" style="flex-wrap:wrap;gap:5px"></div>""",
    """        <div class="row" style="margin-top:6px">
          <div><label>fade <span id="oov">100</span>%</label>
            <input type="range" id="oopac" min="0" max="100" step="5" value="100" style="width:100%"></div>
          <div style="flex:0 0 auto;align-self:end">
            <button class="mini" id="oBack">send back</button>
            <button class="mini" id="oFront">bring front</button></div>
        </div>

        <label style="margin-top:8px">what can be done here
          <span class="hint">&mdash; a permission, not a script</span></label>
        <div id="objCan" class="row" style="flex-wrap:wrap;gap:5px"></div>

        <div id="oLampBox" style="display:none;margin-top:8px;padding-top:8px;
             border-top:1px solid var(--line)">
          <div class="row">
            <div><label>brightness <span id="olpv">100</span>%</label>
              <input type="range" id="oPower" min="10" max="300" step="5" value="100" style="width:100%"></div>
            <div><label>spread <span id="olsv">100</span>%</label>
              <input type="range" id="oSpread" min="20" max="300" step="5" value="100" style="width:100%"></div>
          </div>
          <div class="row" style="margin-top:6px">
            <div><label>bulb height <span id="obhv">auto</span></label>
              <input type="range" id="oBulb" min="-40" max="140" step="2" value="0" style="width:100%"></div>
            <div style="flex:0 0 auto;align-self:end">
              <button class="mini" id="oBulbAuto">auto</button></div>
          </div>
          <div class="hint">turn it with the object's own rotation &mdash; a sconce on its side
            washes the wall instead of the floor</div>
        </div>
        <div class="hint" style="margin-top:6px">arrow keys nudge it a pixel at a time</div>""",
    "fade, depth, and the lamp box")

rep("""      <label style="margin-top:10px">the ground""",
    """      <label style="margin-top:10px">the ground
        <button class="mini" id="lightOff" style="float:right">lighting: on</button>""",
    "the master lighting toggle")

# --- behaviour
rep("""  $('olamp').classList.toggle('pri',!!o.lamp);""",
    """  $('olamp').classList.toggle('pri',!!o.lamp);
  $('oopac').value=o.opacity==null?100:o.opacity;
  $('oov').textContent=o.opacity==null?100:o.opacity;
  $('oLampBox').style.display=o.lamp?'':'none';
  const pw=Math.round((o.lampPower==null?1:o.lampPower)*100);
  const sp=Math.round((o.lampSize==null?1:o.lampSize)*100);
  $('oPower').value=pw; $('olpv').textContent=pw;
  $('oSpread').value=sp; $('olsv').textContent=sp;
  $('oBulb').value=o.bulbY==null?bulbHeightOf(o):o.bulbY;
  $('obhv').textContent=o.bulbY==null?'auto ('+bulbHeightOf(o)+')':o.bulbY;""",
    "sync the new controls")

rep("""$('oflip').onclick=()=>{ const o=scene.objects[osel]; if(o){o.flip=!o.flip; draw()} };""",
    """$('oflip').onclick=()=>{ const o=scene.objects[osel]; if(o){o.flip=!o.flip; draw()} };
$('oopac').oninput=e=>{ const o=scene.objects[osel]; if(o){ o.opacity=+e.target.value;
  $('oov').textContent=e.target.value; draw() } };
// Depth by hand: the slider sets an exact number, these just say "in front of" / "behind".
const depthOf=o=>o.z==null?o.y:o.z;
$('oFront').onclick=()=>{ const o=scene.objects[osel]; if(!o)return;
  o.z=Math.round(Math.max(...scene.objects.map(depthOf))+2); syncObj(); draw() };
$('oBack').onclick=()=>{ const o=scene.objects[osel]; if(!o)return;
  o.z=Math.round(Math.min(...scene.objects.map(depthOf))-2); syncObj(); draw() };
// A lamp's own light. The room's "light size" slider scales every lamp together; these two are
// this one fixture's share of it, which is how one bright sconce sits beside one dim lamp.
$('oPower').oninput=e=>{ const o=scene.objects[osel]; if(o){ o.lampPower=+e.target.value/100;
  $('olpv').textContent=e.target.value; litCache.key=''; draw() } };
$('oSpread').oninput=e=>{ const o=scene.objects[osel]; if(o){ o.lampSize=+e.target.value/100;
  $('olsv').textContent=e.target.value; litCache.key=''; draw() } };
$('oBulb').oninput=e=>{ const o=scene.objects[osel]; if(o){ o.bulbY=+e.target.value;
  $('obhv').textContent=e.target.value; litCache.key=''; draw() } };
$('oBulbAuto').onclick=()=>{ const o=scene.objects[osel]; if(o){ o.bulbY=null;
  litCache.key=''; syncObj(); draw() } };
// todOn already gates litRoom; it simply had no button on this side.
$('lightOff').onclick=()=>{ todOn=!todOn;
  $('lightOff').textContent='lighting: '+(todOn?'on':'off');
  $('lightOff').classList.toggle('pri',!todOn); litCache.key=''; draw() };""",
    "wire the new controls")

rep("""addEventListener('keydown',e=>{
  if(/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;""",
    """addEventListener('keydown',e=>{
  if(/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
  const nud={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1]}[e.key];
  if(nud && osel>=0){ const o=scene.objects[osel];
    const k=e.shiftKey?5:1; o.x+=nud[0]*k; o.y+=nud[1]*k; e.preventDefault(); draw(); return }""",
    "arrow keys nudge")

# the master toggle needs somewhere to live, and the render has to honour it


tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
