"""Give the assembler the scene: place objects, declare what they afford, draw the ground.

The split it settles: the ATTACH EDITOR makes one animation; the ASSEMBLER owns the room -
what stands in it, what each thing may be used for, where he can walk, the light and the
weather. Until now the assembler could only display objects it imported, which is why a scene
built here could never afford anything.
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


rep("let passiveScale=1;",
    """let passiveScale=1;
let osel=-1, drag=null;                 // the object being placed
let zsel=-1, zdraft=null, zmode='', zvert=-1;
const inPoly=(x,y,pts)=>{ let hit=false;
  for(let i=0,j=pts.length-1;i<pts.length;j=i++){
    const [xi,yi]=pts[i],[xj,yj]=pts[j];
    if(((yi>y)!==(yj>y)) && x < (xj-xi)*(y-yi)/((yj-yi)||1e-9)+xi) hit=!hit }
  return hit};
const nearPt=(x,y,p,r)=>Math.abs(x-p[0])<=r && Math.abs(y-p[1])<=r;
const sizeOf=o=>{const d=SD.props[o.object];
  return {w:d.w*o.scale*passiveScale, h:d.h*o.scale*passiveScale}};""",
    "placement state")

# ---- panels ----
rep("""    <div class="panel">
      <label>the room</label>
      <select id="room"></select>
      <div id="objs" style="margin-top:9px"></div>
    </div>""",
    """    <div class="panel">
      <label>the room</label>
      <select id="room"></select>

      <label style="margin-top:10px">put something in it</label>
      <div class="row">
        <select id="addSel"></select>
        <button style="flex:0 0 auto" id="addBtn">+ add</button>
      </div>
      <div id="objs" style="margin-top:9px"></div>

      <div id="objEdit" style="display:none;margin-top:9px;padding-top:9px;
           border-top:1px solid var(--line)">
        <label><span id="objName" style="color:var(--ink);font-weight:600"></span>
          <button class="mini" id="objDel" style="float:right">remove</button></label>
        <div class="row">
          <div><label>size <span id="oszl">100</span>%</label>
            <input type="range" id="osz" min="10" max="400" step="5" value="100" style="width:100%"></div>
          <div><label>turn <span id="orl">0</span>&deg;</label>
            <input type="range" id="orot" min="-180" max="180" step="5" value="0" style="width:100%"></div>
        </div>
        <div class="row" style="margin-top:6px">
          <div><label>depth <span id="ozl">auto</span></label>
            <input type="range" id="oz" min="0" max="240" step="1" value="200" style="width:100%"></div>
          <div style="flex:0 0 auto;align-self:end">
            <button class="mini" id="ozAuto">auto</button>
            <button class="mini" id="oflip">flip</button>
            <button class="mini" id="olamp">light</button></div>
        </div>
        <label style="margin-top:8px">what can be done here</label>
        <div id="objCan" class="row" style="flex-wrap:wrap;gap:5px"></div>
      </div>

      <label style="margin-top:10px">the ground
        <span class="hint">&mdash; click round a shape, then its first corner again</span></label>
      <div class="row">
        <button class="mini" id="zFloor">+ walkable</button>
        <button class="mini" id="zKeep">+ keep out</button>
        <button class="mini" id="zClose">finish</button>
        <button class="mini" id="zDel">remove</button>
      </div>
      <div class="hint" id="zList" style="margin-top:6px"></div>
    </div>""",
    "scene panels")

# ---- interaction on the canvas ----
rep("""function draw(){""",
    """const at=e=>{ const r=cv.getBoundingClientRect();
  return {x:Math.round((e.clientX-r.left)/r.width*GAME.w),
          y:Math.round((e.clientY-r.top)/r.height*GAME.h)} };
const pickObj=q=>{ for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i], {w,h}=sizeOf(o);
    if(q.x>=o.x-w/2&&q.x<=o.x+w/2&&q.y>=o.y-h&&q.y<=o.y) return i } return -1 };
function closeZone(){
  if(zdraft && zdraft.pts.length>=3){ scene.zones.push({kind:zdraft.kind,pts:zdraft.pts});
    zsel=scene.zones.length-1 }
  zdraft=null; zmode=''; zvert=-1;
  $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri'); draw();
}
cv.addEventListener('pointerdown',e=>{
  const q=at(e);
  if(zmode){ if(!zdraft) zdraft={kind:zmode,pts:[]};
    const first=zdraft.pts[0];
    if(first && zdraft.pts.length>=3 && nearPt(q.x,q.y,first,5)){ closeZone(); return }
    zdraft.pts.push([q.x,q.y]); draw(); return }
  if(zsel>=0 && scene.zones[zsel]){
    const vi=scene.zones[zsel].pts.findIndex(p=>nearPt(q.x,q.y,p,5));
    if(vi>=0){ zvert=vi; cv.setPointerCapture(e.pointerId); draw(); return } }
  const hit=pickObj(q);
  if(hit>=0){ osel=hit; syncObj(); drag={dx:scene.objects[hit].x-q.x, dy:scene.objects[hit].y-q.y};
    cv.setPointerCapture(e.pointerId); draw(); return }
  for(let i=scene.zones.length-1;i>=0;i--){
    const z=scene.zones[i];
    if(z.pts&&z.pts.length>2&&inPoly(q.x,q.y,z.pts)){ zsel=i; zvert=-1; osel=-1; syncObj(); draw(); return } }
  osel=-1; syncObj(); draw();
});
cv.addEventListener('pointermove',e=>{
  const q=at(e);
  if(zvert>=0 && zsel>=0){ scene.zones[zsel].pts[zvert]=[q.x,q.y]; draw(); return }
  if(drag && osel>=0){ const o=scene.objects[osel]; o.x=q.x+drag.dx; o.y=q.y+drag.dy; draw() }
});
cv.addEventListener('pointerup',()=>{ drag=null; zvert=-1 });
addEventListener('keydown',e=>{
  if(/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
  if(e.key==='Escape'&&(zdraft||zmode)){ zdraft=null; zmode='';
    $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri'); draw() }
  else if(e.key==='Enter'&&zdraft) closeZone();
  else if((e.key==='Delete'||e.key==='Backspace')&&osel>=0){
    scene.objects.splice(osel,1); osel=-1; syncObj(); draw() }
});

function syncObj(){
  const o=scene.objects[osel];
  $('objEdit').style.display=o?'':'none';
  if(!o) return;
  $('objName').textContent=o.object;
  $('osz').value=Math.round(o.scale*100); $('oszl').textContent=Math.round(o.scale*100);
  $('orot').value=o.rot||0; $('orl').textContent=o.rot||0;
  $('oz').value=o.z==null?Math.round(o.y):o.z; $('ozl').textContent=o.z==null?'auto':o.z;
  $('olamp').classList.toggle('pri',!!o.lamp);
  $('objCan').innerHTML=AFFORD.map(k=>`<button class="mini" data-can="${k}" style="${
    (o.can||[]).includes(k)?'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
    >${k}</button>`).join('');
  $('objCan').querySelectorAll('[data-can]').forEach(b=>b.onclick=()=>{
    const set=new Set(o.can||[]); const k=b.dataset.can;
    set.has(k)?set.delete(k):set.add(k); o.can=[...set]; syncObj(); draw() });
}
$('addBtn').onclick=async()=>{ const n=$('addSel').value; if(!SD.props[n]) return;
  await load(SD.props[n].img,'prop|'+n);
  const d=SD.props[n];
  scene.objects.push({object:n, x:GAME.w/2, y:GAME.floor+12,
    scale:+(28/Math.max(d.w,d.h)).toFixed(2), opacity:100, flip:false,
    lamp:SD.props[n].kind==='light'||undefined, can:null});
  osel=scene.objects.length-1; syncObj(); draw() };
$('objDel').onclick=()=>{ if(osel<0)return; scene.objects.splice(osel,1); osel=-1; syncObj(); draw() };
$('osz').oninput=e=>{ const o=scene.objects[osel]; if(o){o.scale=+e.target.value/100;
  $('oszl').textContent=e.target.value; draw()} };
$('orot').oninput=e=>{ const o=scene.objects[osel]; if(o){o.rot=+e.target.value;
  $('orl').textContent=e.target.value; draw()} };
$('oz').oninput=e=>{ const o=scene.objects[osel]; if(o){o.z=+e.target.value;
  $('ozl').textContent=e.target.value; draw()} };
$('ozAuto').onclick=()=>{ const o=scene.objects[osel]; if(o){o.z=null; syncObj(); draw()} };
$('oflip').onclick=()=>{ const o=scene.objects[osel]; if(o){o.flip=!o.flip; draw()} };
$('olamp').onclick=()=>{ const o=scene.objects[osel]; if(o){o.lamp=!o.lamp;
  litCache.key=''; syncObj(); draw()} };
$('zFloor').onclick=()=>{ zdraft=null; zmode=zmode==='floor'?'':'floor';
  $('zFloor').classList.toggle('pri',zmode==='floor'); $('zKeep').classList.remove('pri'); draw() };
$('zKeep').onclick=()=>{ zdraft=null; zmode=zmode==='keepout'?'':'keepout';
  $('zKeep').classList.toggle('pri',zmode==='keepout'); $('zFloor').classList.remove('pri'); draw() };
$('zClose').onclick=closeZone;
$('zDel').onclick=()=>{ if(zsel>=0){ scene.zones.splice(zsel,1); zsel=-1; draw() } };

function draw(){""",
    "placement and zone editing")

# ---- draw the draft shape and selection ----
rep("""  // the things in the room, back to front""",
    """  if(zdraft&&zdraft.pts.length){
    ctx.beginPath();
    zdraft.pts.forEach((p,i)=>i?ctx.lineTo(p[0]*Z,p[1]*Z):ctx.moveTo(p[0]*Z,p[1]*Z));
    ctx.strokeStyle='#fff'; ctx.lineWidth=2; ctx.setLineDash([4,4]); ctx.stroke(); ctx.setLineDash([]);
    zdraft.pts.forEach(p=>{ctx.fillStyle='#fff';ctx.fillRect(p[0]*Z-3,p[1]*Z-3,6,6)});
    const f=zdraft.pts[0]; ctx.strokeRect(f[0]*Z-5,f[1]*Z-5,10,10);
  }
  if(zsel>=0&&scene.zones[zsel]) scene.zones[zsel].pts.forEach(p=>{
    ctx.fillStyle='#fff'; ctx.fillRect(p[0]*Z-3,p[1]*Z-3,6,6);
    ctx.strokeStyle='#000'; ctx.lineWidth=1; ctx.strokeRect(p[0]*Z-3,p[1]*Z-3,6,6) });

  // the things in the room, back to front""",
    "draw the draft")

rep("""    const w=d.w*o.scale*passiveScale, h=d.h*o.scale*passiveScale;
    ctx.save(); ctx.globalAlpha=(o.opacity==null?100:o.opacity)/100;
    ctx.translate(o.x*Z,o.y*Z); if(o.flip) ctx.scale(-1,1);""",
    """    const w=d.w*o.scale*passiveScale, h=d.h*o.scale*passiveScale;
    ctx.save(); ctx.globalAlpha=(o.opacity==null?100:o.opacity)/100;
    ctx.translate(o.x*Z,o.y*Z);
    if(o.rot) ctx.rotate(o.rot*Math.PI/180);
    if(o.flip) ctx.scale(-1,1);""",
    "objects can turn")

rep("""  const items=[...scene.objects].sort((a,b)=>(a.z==null?a.y:a.z)-(b.z==null?b.y:b.z));
  for(const o of items){""",
    """  const items=[...scene.objects].sort((a,b)=>(a.z==null?a.y:a.z)-(b.z==null?b.y:b.z));
  for(const o of items){
    if(scene.objects[osel]===o){ const {w,h}=sizeOf(o);
      ctx.strokeStyle='#e8392d'; ctx.lineWidth=2; ctx.setLineDash([5,4]);
      ctx.strokeRect((o.x-w/2)*Z-2,(o.y-h)*Z-2,w*Z+4,h*Z+4); ctx.setLineDash([]); }""",
    "show which object is selected")

rep("""  drawAnims(); drawObjs();""",
    """  { const zs=scene.zones;
    $('zList').innerHTML = zs.length
      ? zs.map((z,i)=>`<button class="mini" data-z="${i}" style="${i===zsel?
          'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
          >${z.kind==='floor'?'walkable':'keep out'} ${(z.pts||[]).length}</button>`).join(' ')
      : 'none yet \\u2014 without a walkable shape the device keeps its own band';
    $('zList').querySelectorAll('[data-z]').forEach(b=>b.onclick=()=>{
      zsel=+b.dataset.z; osel=-1; syncObj(); draw() }); }
  drawAnims(); drawObjs();""",
    "zone list")

rep("""$('room').innerHTML=Object.keys(SD.rooms).map(k=>`<option>${k}</option>`).join('');""",
    """{ const byKind=w=>Object.keys(SD.props).filter(k=>(SD.props[k].kind||'prop')===w)
    .map(k=>`<option>${k}</option>`).join('');
  $('addSel').innerHTML=`<optgroup label="things">${byKind('prop')}</optgroup>`+
    `<optgroup label="lights">${byKind('light')}</optgroup>`; }
$('room').innerHTML=Object.keys(SD.rooms).map(k=>`<option>${k}</option>`).join('');""",
    "populate the object picker")

# objects list becomes clickable
rep("""    ? scene.objects.map(o=>`<div class="obj">${o.object}""",
    """    ? scene.objects.map((o,i)=>`<div class="obj" data-o="${i}" style="cursor:pointer;${
        i===osel?'color:var(--accent);font-weight:600':''}">${o.object}""",
    "objects are clickable")
rep("""    : '<div class="hint">import a scene to see what is in the room</div>';""",
    """    : '<div class="hint">nothing in the room yet \\u2014 add something above, or import a scene</div>';
  $('objs').querySelectorAll('[data-o]').forEach(e=>e.onclick=()=>{
    osel=+e.dataset.o; zsel=-1; syncObj(); draw() });""",
    "click an object in the list")

rep("""      scale:+(+o.scale*passiveScale).toFixed(3), authoredScale:+(+o.scale).toFixed(3),""",
    """      scale:+(+o.scale*passiveScale).toFixed(3), authoredScale:+(+o.scale).toFixed(3),
      rotation:o.rot||0, lamp:!!o.lamp,""",
    "rotation and lamp in the export")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
