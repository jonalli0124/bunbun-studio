"""A pixel editor inside Animation Creation - draw a prop, attach it, ship it.

Jon: "can we add a pixel editor to the animation creation?" A kid draws a cookie, a hat, a
wand - and it becomes a prop like any other: attachable to the character, baked into the
composed frames, carried in the saved animation file, imported by the Scene Assembler, and
sent to the device by the button. No external art tool in the loop.

The editor is an overlay: 16/24/32 grid, pencil/eraser/fill/eyedropper, a 16-colour bunbun
palette + free colour, mirror-X for symmetric things, undo, and "start from" any existing
prop. Saving registers the drawing as DATA.props[name] with kind 'prop' and custom:true.

Persistence: project() now carries customProps {name: dataURL, w, h}; loadProject injects
them back into DATA.props BEFORE the layers are validated, so a reopened animation still
owns every prop it wears. The autosave rides project(), so drawings survive a refresh too.
"""
import io, os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "attach_editor.html")
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


# ---------------------------------------------------------------- the button, by + add
rep("""      <div class="row" style="margin-top:8px">
        <select id="addProp"></select>
        <button style="flex:0 0 auto" class="pri" id="add">+ add</button>
      </div>""",
    """      <div class="row" style="margin-top:8px">
        <select id="addProp"></select>
        <button style="flex:0 0 auto" class="pri" id="add">+ add</button>
        <button style="flex:0 0 auto" id="pxOpen" title="draw your own prop, right here">&#127912;</button>
      </div>""",
    "the draw button")

# ---------------------------------------------------------------- the overlay markup + css
rep("""</style>""",
    """  /* ---- the pixel editor overlay ---- */
  #pxWrap{position:fixed;inset:0;background:rgba(20,16,12,.55);z-index:60;display:none;
    align-items:center;justify-content:center}
  #pxWrap.on{display:flex}
  #pxPanel{background:var(--panel);border:1px solid var(--line);border-radius:12px;
    padding:14px;display:flex;gap:14px;max-width:min(96vw,720px);max-height:92vh;overflow:auto}
  #pxCv{image-rendering:pixelated;border:1px solid var(--line);border-radius:4px;
    background:repeating-conic-gradient(#0000 0 25%,#8883 0 50%) 0 0/12px 12px;cursor:crosshair}
  .pxSide{display:flex;flex-direction:column;gap:8px;min-width:170px}
  .pxPal{display:grid;grid-template-columns:repeat(8,22px);gap:3px}
  .pxPal .c{width:22px;height:22px;border-radius:4px;border:1px solid var(--line);cursor:pointer}
  .pxPal .c.sel{outline:2px solid var(--accent)}
  .pxTools{display:flex;gap:4px;flex-wrap:wrap}
  .pxTools button.sel{background:var(--accent);color:#fff;border-color:var(--accent)}
</style>""",
    "the overlay css")

rep("""<script>/*ATTACH_DATA*/</script>""",
    """<div id="pxWrap">
  <div id="pxPanel">
    <div><canvas id="pxCv" width="384" height="384"></canvas></div>
    <div class="pxSide">
      <label>your prop's name</label>
      <input id="pxName" placeholder="e.g. cookie, magic wand" style="padding:7px;font:inherit;
        border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink)">
      <div class="row"><div><label>size</label>
        <select id="pxSize"><option>16</option><option selected>24</option><option>32</option></select></div>
        <div><label>start from</label><select id="pxFrom"><option value="">blank</option></select></div>
      </div>
      <div class="pxTools">
        <button id="pxPen" class="sel" title="draw">&#9998;</button>
        <button id="pxEra" title="erase">&#9723;</button>
        <button id="pxFill" title="fill">&#129700;</button>
        <button id="pxPick" title="pick a colour off the drawing">&#128393;</button>
        <button id="pxMirror" title="draw both halves at once">&#8646; mirror</button>
        <button id="pxUndo" title="undo">&#8630;</button>
        <button id="pxClear" title="wipe it">&#10006;</button>
      </div>
      <div class="pxPal" id="pxPal"></div>
      <div class="row" style="align-items:center">
        <label style="flex:0 0 auto;margin:0">more colours</label>
        <input type="color" id="pxCustom" value="#8a5a2b" style="width:40px;height:26px;padding:0;
          border:1px solid var(--line);border-radius:6px;background:var(--bg)">
      </div>
      <div class="row" style="margin-top:auto">
        <button class="pri" id="pxSave">keep it</button>
        <button id="pxClose">close</button>
      </div>
      <div class="hint">it becomes a prop like any other &mdash; attach it, bake it, send it
        to the device</div>
    </div>
  </div>
</div>
<script>/*ATTACH_DATA*/</script>""",
    "the overlay markup")

# ---------------------------------------------------------------- the editor logic
rep("""const project=()=>Object.assign({format:'bunbun-attach-project',v:1,""",
    """// ---- THE PIXEL EDITOR ----
const PX={n:24, grid:null, tool:'pen', color:'#8a5a2b', mirror:false, hist:[], drawing:false};
const PX_PAL=['#241f1a','#6d685a','#b3a98e','#f6f1e4','#8a5a2b','#c98a4b','#e8b06b','#f4d8a8',
  '#c0392b','#e8501e','#f0c84a','#7a9e3b','#27ae60','#2a7ab8','#8a6ee6','#fd6bb0'];
function pxInit(){
  const pal=$('pxPal');
  pal.innerHTML=PX_PAL.map(c=>`<div class="c" data-c="${c}" style="background:${c}"></div>`).join('');
  pal.querySelectorAll('.c').forEach(el=>el.onclick=()=>{ PX.color=el.dataset.c;
    pal.querySelectorAll('.c').forEach(x=>x.classList.toggle('sel',x===el)); pxTool('pen'); });
  $('pxCustom').oninput=e=>{ PX.color=e.target.value; pxTool('pen');
    pal.querySelectorAll('.c').forEach(x=>x.classList.remove('sel')); };
  pxBlank(24);
}
function pxBlank(n){ PX.n=n; PX.grid=Array.from({length:n},()=>Array(n).fill(null)); pxPaint(); }
function pxSnapshot(){ PX.hist.push(PX.grid.map(r=>r.slice())); if(PX.hist.length>60) PX.hist.shift(); }
function pxPaint(){
  const cv=$('pxCv'), g=cv.getContext('2d'), n=PX.n, cell=cv.width/n;
  g.clearRect(0,0,cv.width,cv.height);
  for(let y=0;y<n;y++) for(let x=0;x<n;x++)
    if(PX.grid[y][x]){ g.fillStyle=PX.grid[y][x]; g.fillRect(x*cell,y*cell,cell,cell); }
  g.strokeStyle='rgba(128,120,100,.25)'; g.lineWidth=1;
  for(let i=1;i<n;i++){ g.beginPath(); g.moveTo(i*cell,0); g.lineTo(i*cell,cv.height); g.stroke();
    g.beginPath(); g.moveTo(0,i*cell); g.lineTo(cv.width,i*cell); g.stroke(); }
}
function pxCell(e){ const r=$('pxCv').getBoundingClientRect(), n=PX.n;
  const x=Math.floor((e.clientX-r.left)/r.width*n), y=Math.floor((e.clientY-r.top)/r.height*n);
  return (x>=0&&x<n&&y>=0&&y<n)?{x,y}:null; }
function pxApply(x,y){
  const n=PX.n;
  const put=(cx,cy,v)=>{ if(cx>=0&&cx<n&&cy>=0&&cy<n) PX.grid[cy][cx]=v; };
  if(PX.tool==='pen'){ put(x,y,PX.color); if(PX.mirror) put(n-1-x,y,PX.color); }
  else if(PX.tool==='era'){ put(x,y,null); if(PX.mirror) put(n-1-x,y,null); }
  else if(PX.tool==='pick'){ if(PX.grid[y][x]){ PX.color=PX.grid[y][x]; $('pxCustom').value=PX.color; }
    pxTool('pen'); return; }
  else if(PX.tool==='fill'){
    const from=PX.grid[y][x], to=PX.color; if(from===to) return;
    const q=[[x,y]], seen=new Set();
    while(q.length){ const [cx,cy]=q.pop(), k=cx+','+cy;
      if(seen.has(k)||cx<0||cx>=n||cy<0||cy>=n||PX.grid[cy][cx]!==from) continue;
      seen.add(k); PX.grid[cy][cx]=to; q.push([cx+1,cy],[cx-1,cy],[cx,cy+1],[cx,cy-1]); }
  }
  pxPaint();
}
function pxTool(t){ PX.tool=t;
  const map={pen:'pxPen',era:'pxEra',fill:'pxFill',pick:'pxPick'};
  for(const k in map) $(map[k]).classList.toggle('sel',k===t); }
function pxToDataURL(){
  const n=PX.n, c=document.createElement('canvas'); c.width=n; c.height=n;
  const g=c.getContext('2d');
  for(let y=0;y<n;y++) for(let x=0;x<n;x++)
    if(PX.grid[y][x]){ g.fillStyle=PX.grid[y][x]; g.fillRect(x,y,1,1); }
  return c.toDataURL('image/png');
}
// registering a drawing (or a loaded custom prop) as a first-class prop
function registerCustomProp(name, dataURL, w, h){
  DATA.props[name]={img:dataURL, w:w, h:h, kind:'prop', custom:true};
  load(dataURL, imgs, 'prop|'+name);
  let grp=$('addProp').querySelector('optgroup[label="your drawings"]');
  if(!grp){ grp=document.createElement('optgroup'); grp.label='your drawings';
    $('addProp').prepend(grp); }
  if(![...grp.children].some(o=>o.value===name)){
    const o=document.createElement('option'); o.textContent=name; grp.prepend(o); }
}
function pxWire(){
  pxInit();
  const cv=$('pxCv');
  cv.addEventListener('pointerdown',e=>{ const c=pxCell(e); if(!c) return;
    pxSnapshot(); PX.drawing=true; pxApply(c.x,c.y); cv.setPointerCapture(e.pointerId); });
  cv.addEventListener('pointermove',e=>{ if(!PX.drawing) return;
    const c=pxCell(e); if(c&&(PX.tool==='pen'||PX.tool==='era')) pxApply(c.x,c.y); });
  cv.addEventListener('pointerup',()=>{ PX.drawing=false });
  $('pxPen').onclick=()=>pxTool('pen'); $('pxEra').onclick=()=>pxTool('era');
  $('pxFill').onclick=()=>pxTool('fill'); $('pxPick').onclick=()=>pxTool('pick');
  $('pxMirror').onclick=()=>{ PX.mirror=!PX.mirror;
    $('pxMirror').classList.toggle('sel',PX.mirror); };
  $('pxUndo').onclick=()=>{ const h=PX.hist.pop(); if(h){ PX.grid=h; pxPaint(); } };
  $('pxClear').onclick=()=>{ pxSnapshot(); pxBlank(PX.n); };
  $('pxSize').onchange=e=>{ pxSnapshot(); pxBlank(+e.target.value); };
  $('pxFrom').onchange=async e=>{
    const nm=e.target.value; if(!nm) return;
    if(!imgs['prop|'+nm]) await load(DATA.props[nm].img, imgs, 'prop|'+nm);
    const im=imgs['prop|'+nm], n=PX.n;
    const c=document.createElement('canvas'); c.width=n; c.height=n;
    const g=c.getContext('2d',{willReadFrequently:true}); g.imageSmoothingEnabled=false;
    g.drawImage(im,0,0,n,n);
    const d=g.getImageData(0,0,n,n).data;
    pxSnapshot();
    for(let y=0;y<n;y++) for(let x=0;x<n;x++){ const i=(y*n+x)*4;
      PX.grid[y][x]=d[i+3]>=128?('#'+[d[i],d[i+1],d[i+2]].map(v=>v.toString(16).padStart(2,'0')).join('')):null; }
    pxPaint();
  };
  $('pxOpen').onclick=()=>{
    $('pxFrom').innerHTML='<option value="">blank</option>'+
      Object.keys(DATA.props).sort().map(k=>`<option>${k}</option>`).join('');
    $('pxWrap').classList.add('on');
  };
  $('pxClose').onclick=()=>$('pxWrap').classList.remove('on');
  $('pxSave').onclick=async()=>{
    const nm=($('pxName').value||'').trim().toLowerCase().replace(/[^a-z0-9 _-]/g,'').slice(0,20);
    if(!nm) return flash('give your drawing a name first',true);
    if(!PX.grid.some(r=>r.some(Boolean))) return flash('draw something first',true);
    registerCustomProp(nm, pxToDataURL(), PX.n, PX.n);
    await new Promise(r=>setTimeout(r,60));      // the img finishes loading
    $('addProp').value=nm;
    $('pxWrap').classList.remove('on');
    flash(`"${nm}" is a prop now — + add puts it on the character`);
    autosave();
  };
}
pxWire();

const project=()=>Object.assign({format:'bunbun-attach-project',v:1,""",
    "the editor")

# ---------------------------------------------------------------- persistence
rep("""   order:order.map(o=>({i:o.i,skip:!!o.skip,cs:csOf(o)})),   // no ids: regenerated on load
   layers:stack.map(it=>it.char?'CHARACTER':authored(it))});""",
    """   order:order.map(o=>({i:o.i,skip:!!o.skip,cs:csOf(o)})),   // no ids: regenerated on load
   layers:stack.map(it=>it.char?'CHARACTER':authored(it)),
   // the child's own drawings ride along, so a reopened animation still owns every prop it
   // wears - and the Scene Assembler can learn them on import
   customProps:(()=>{ const cp={};
     for(const k of Object.keys(DATA.props)) if(DATA.props[k].custom)
       cp[k]={img:DATA.props[k].img, w:DATA.props[k].w, h:DATA.props[k].h};
     return Object.keys(cp).length?cp:undefined; })()});""",
    "drawings ride in the file")

rep("""  if(!DATA.clips[p.clip]) throw new Error(`clip "${p.clip}" is not in this data set`);""",
    """  if(!DATA.clips[p.clip]) throw new Error(`clip "${p.clip}" is not in this data set`);
  // the file's own drawings register FIRST, so its layers validate against them
  if(p.customProps) for(const k of Object.keys(p.customProps)){
    const c=p.customProps[k];
    if(c&&c.img) registerCustomProp(k, c.img, c.w||24, c.h||24);
  }""",
    "and register on load")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
