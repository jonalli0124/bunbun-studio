"""Room objects for the attach editor: props placed in the ROOM, not on the character.

Character-attached props follow a hand. Room objects sit at a room coordinate and stay there —
a tub, a rug, a bowl — with their own scale, rotation, flip and opacity. Depth follows y, the
way the game itself sorts, so a prop lower on the floor draws in front.
"""
import io, os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "attach_editor.html")
s = io.open(SRC, encoding="utf-8").read()
orig = s


def rep(old, new, label):
    global s
    assert old in s, "NO MATCH: " + label
    s = s.replace(old, new, 1)
    print("  ok:", label)


# ---------- state ----------
rep("let gRoom='', gScale=null, gX=160, gY=GAME.floor;",
    """let gRoom='', gScale=null, gX=160, gY=GAME.floor;
// Room objects live in ROOM coordinates and ignore the character entirely. Depth is by y,
// the same painter's rule the game uses, so something nearer the front draws in front.
let rprops=[], rsel=-1;
const rpDefaults=n=>({prop:n,x:160,y:GAME.floor,scale:1,rot:0,flip:false,opac:100});""",
    "room prop state")

# ---------- markup ----------
rep("""      <div class="row" style="margin-top:8px">
        <div><label>scale everything <span class="val" id="gsv">145</span>%""",
    """      <div class="row" style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <select id="rpAdd"></select>
        <button style="flex:0 0 auto" id="rpNew">+ put in the room</button>
      </div>
      <div id="rpList" style="display:flex;flex-wrap:wrap;gap:5px;margin-top:8px"></div>
      <div id="rpEdit" style="display:none;margin-top:8px">
        <div class="row">
          <div><label>size <span class="val" id="rpsv">100</span>%</label>
            <input type="range" id="rpScale" min="10" max="400" step="5" value="100"></div>
          <div><label>turn <span class="val" id="rprv">0</span>&deg;</label>
            <input type="range" id="rpRot" min="-180" max="180" step="5" value="0"></div>
        </div>
        <div class="row" style="margin-top:6px">
          <div><label>fade <span class="val" id="rpov">100</span>%</label>
            <input type="range" id="rpOpac" min="0" max="100" step="5" value="100"></div>
          <div style="flex:0 0 auto;align-self:end">
            <button class="mini" id="rpFlip">flip &harr;</button>
            <button class="mini" id="rpDel" style="background:var(--warn);color:#fff">remove</button></div>
        </div>
        <div class="hint" id="rpWhere"></div>
      </div>

      <div class="row" style="margin-top:8px">
        <div><label>scale everything <span class="val" id="gsv">145</span>%""",
    "room prop markup")

# ---------- drawing ----------
rep("""  const src=composeFrame(fi,seq,1,'pad');
  const sc=(gScale==null?defaultScale():gScale)/100;
  const feet=PAD+90, midx=PAD+S/2;
  g.drawImage(src, (gX-midx*sc)*Zg, (gY-feet*sc)*Zg, FULL*sc*Zg, FULL*sc*Zg);""",
    """  const src=composeFrame(fi,seq,1,'pad');
  const sc=(gScale==null?defaultScale():gScale)/100;
  const feet=PAD+90, midx=PAD+S/2;
  // everything sorts by y, so what is lower in the room draws in front
  const items=rprops.map((p,i)=>({y:p.y,go:()=>drawRoomProp(g,p,Zg,i===rsel)}));
  items.push({y:gY,go:()=>g.drawImage(src,(gX-midx*sc)*Zg,(gY-feet*sc)*Zg,FULL*sc*Zg,FULL*sc*Zg)});
  items.sort((a,b)=>a.y-b.y).forEach(it=>it.go());""",
    "draw room props with the character")

rep("function drawGame(fi,seq){",
    """function rpSize(p){ const d=DATA.props[p.prop]; const k=p.scale;
  return {w:d.w*k, h:d.h*k} }
function drawRoomProp(g,p,Zg,selected){
  const d=DATA.props[p.prop], im=imgs['prop|'+p.prop];
  if(!im||!im.complete||!im.naturalWidth) return;
  const {w,h}=rpSize(p);
  g.save(); g.imageSmoothingEnabled=false;
  g.globalAlpha=(p.opac==null?100:p.opac)/100;
  g.translate(p.x*Zg, p.y*Zg);
  if(p.rot) g.rotate(p.rot*Math.PI/180);
  if(p.flip) g.scale(-1,1);
  g.drawImage(im, -w/2*Zg, -h*Zg, w*Zg, h*Zg);   // stands on its own base
  g.restore();
  if(selected){
    g.strokeStyle='#e8392d'; g.lineWidth=2; g.setLineDash([5,4]);
    g.strokeRect((p.x-w/2)*Zg-2,(p.y-h)*Zg-2,w*Zg+4,h*Zg+4); g.setLineDash([]);
  }
}
function drawGame(fi,seq){""",
    "drawRoomProp")

# ---------- list, controls, hit-testing ----------
rep("$('gscale').oninput=e=>{ gScale=+e.target.value; render() };",
    """function rpDraw(){
  $('rpList').innerHTML=rprops.map((p,i)=>
    `<button class="mini" data-rp="${i}" style="${i===rsel?
      'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
      >${p.prop.replace(/_/g,' ')}</button>`).join('')
    || '<span class="hint">nothing in the room yet</span>';
  $('rpList').querySelectorAll('[data-rp]').forEach(b=>b.onclick=()=>{
    rsel=+b.dataset.rp; rpSync(); render() });
  $('rpEdit').style.display = (rsel>=0&&rprops[rsel]) ? '' : 'none';
  const p=rprops[rsel];
  if(p){ $('rpsv').textContent=Math.round(p.scale*100); $('rprv').textContent=p.rot;
    $('rpov').textContent=p.opac==null?100:p.opac;
    const {w,h}=rpSize(p);
    $('rpWhere').textContent=`at ${Math.round(p.x)},${Math.round(p.y)} `+
      `\\u00b7 ${Math.round(w)}\\u00d7${Math.round(h)} px \\u00b7 drag it in the room`; }
}
function rpSync(){ const p=rprops[rsel]; if(!p) return;
  $('rpScale').value=Math.round(p.scale*100); $('rpRot').value=p.rot;
  $('rpOpac').value=p.opac==null?100:p.opac; }
$('rpNew').onclick=()=>{ const n=$('rpAdd').value; if(!DATA.props[n]) return;
  if(!imgs['prop|'+n]) load(DATA.props[n].img,imgs,'prop|'+n).then(render);
  const d=DATA.props[n];
  const p=rpDefaults(n); p.scale=+(24/Math.max(d.w,d.h)).toFixed(2);   // start about 24px
  rprops.push(p); rsel=rprops.length-1; rpSync(); render(); };
$('rpScale').oninput=e=>{ const p=rprops[rsel]; if(p){p.scale=+e.target.value/100; render()} };
$('rpRot').oninput=e=>{ const p=rprops[rsel]; if(p){p.rot=+e.target.value; render()} };
$('rpOpac').oninput=e=>{ const p=rprops[rsel]; if(p){p.opac=+e.target.value; render()} };
$('rpFlip').onclick=()=>{ const p=rprops[rsel]; if(p){p.flip=!p.flip; render()} };
$('rpDel').onclick=()=>{ if(rsel<0)return; rprops.splice(rsel,1); rsel=-1; rpSync(); render() };
$('gscale').oninput=e=>{ gScale=+e.target.value; render() };""",
    "room prop controls")

# the game view drag must pick a room object first, and fall back to moving the character
rep("""(()=>{ const gv=$('gameview'); let dg=false;
  const at=e=>{ const r=gv.getBoundingClientRect();
    return { x:Math.round((e.clientX-r.left)/r.width*GAME.w),
             y:Math.round((e.clientY-r.top)/r.height*GAME.h) } };
  gv.style.cursor='grab';
  gv.addEventListener('pointerdown',e=>{ dg=true; gv.setPointerCapture(e.pointerId);
    gv.style.cursor='grabbing'; const p=at(e); gX=p.x; gY=p.y; render() });
  gv.addEventListener('pointermove',e=>{ if(!dg)return; const p=at(e); gX=p.x; gY=p.y; render() });
  gv.addEventListener('pointerup',()=>{ dg=false; gv.style.cursor='grab' });
})();""",
    """(()=>{ const gv=$('gameview'); let dg=null;
  const at=e=>{ const r=gv.getBoundingClientRect();
    return { x:Math.round((e.clientX-r.left)/r.width*GAME.w),
             y:Math.round((e.clientY-r.top)/r.height*GAME.h) } };
  // topmost room object under the cursor wins; nothing there means you are moving the character
  const pick=q=>{ for(let i=rprops.length-1;i>=0;i--){ const p=rprops[i], {w,h}=rpSize(p);
      if(q.x>=p.x-w/2&&q.x<=p.x+w/2&&q.y>=p.y-h&&q.y<=p.y) return i } return -1 };
  gv.style.cursor='grab';
  gv.addEventListener('pointerdown',e=>{ const q=at(e); const hit=pick(q);
    gv.setPointerCapture(e.pointerId); gv.style.cursor='grabbing';
    if(hit>=0){ rsel=hit; rpSync(); dg={kind:'prop',dx:rprops[hit].x-q.x,dy:rprops[hit].y-q.y} }
    else { rsel=-1; dg={kind:'char'}; gX=q.x; gY=q.y }
    render() });
  gv.addEventListener('pointermove',e=>{ if(!dg)return; const q=at(e);
    if(dg.kind==='prop'){ const p=rprops[rsel]; if(p){ p.x=q.x+dg.dx; p.y=q.y+dg.dy } }
    else { gX=q.x; gY=q.y }
    render() });
  gv.addEventListener('pointerup',()=>{ dg=null; gv.style.cursor='grab' });
})();""",
    "drag picks a room object first")

# keep the list in step, and populate the picker
rep("  $('gsnote').textContent=` (game uses ${defaultScale()}% for the ${guessPhase()})`;",
    "  $('gsnote').textContent=` (game uses ${defaultScale()}% for the ${guessPhase()})`;\n"
    "  rpDraw();",
    "refresh the list each render")
rep("""  rk.forEach(k=>load(DATA.rooms[k],imgs,'room|'+k));""",
    """  rk.forEach(k=>load(DATA.rooms[k],imgs,'room|'+k));
  $('rpAdd').innerHTML=Object.keys(DATA.props).map(k=>`<option>${k}</option>`).join('');
  Object.keys(DATA.props).forEach(k=>load(DATA.props[k].img,imgs,'prop|'+k));""",
    "populate the room-object picker")

# ---------- persistence ----------
rep("  {clip,fps,character:{...CH},",
    "  {clip,fps,character:{...CH},room:gRoom,roomObjects:rprops.map(p=>({...p})),\n"
    "   stage:{x:gX,y:gY,scale:gScale},",
    "save room objects")
rep("    fps=p.fps||7; $('fps').value=fps; pos=0; stack=[]; multi.clear();",
    """    fps=p.fps||7; $('fps').value=fps; pos=0; stack=[]; multi.clear();
    rprops=Array.isArray(p.roomObjects)?p.roomObjects.filter(o=>o&&DATA.props[o.prop])
      .map(o=>Object.assign(rpDefaults(o.prop),o)):[];
    rsel=-1;
    if(p.room&&DATA.rooms&&DATA.rooms[p.room]){ gRoom=p.room; $('room').value=gRoom }
    if(p.stage){ gX=+p.stage.x||gX; gY=+p.stage.y||gY;
      gScale=(p.stage.scale==null?null:+p.stage.scale) }""",
    "load room objects")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written:", len(s), "bytes (was", len(orig), ")")
