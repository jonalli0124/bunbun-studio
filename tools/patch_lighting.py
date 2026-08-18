"""Port the builder's lighting and weather into the attach editor's game view.

The builder is the gold standard for lighting (bunbun-nightly/BUILDER-COMING-TO-LIFE.md), so
these functions are LIFTED from it rather than rewritten — daylight() carries the firmware line
it came from, and litRoom's dim is the firmware's own rainAmount*daylight*0.14 so a shower
darkens an afternoon and never stacks onto night.

Deliberately NOT ported: the lamp system (lampGeoms/bulbHeightOf/MAKE LAMP). It needs placed
light-source objects, which this editor has no concept of. Everything here is the time-of-day
curve, the night regrade, and the weather.
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


LIGHTING = r"""
// ================= lighting & weather, ported from the scene builder =================
// The builder is the lighting record; these are its functions, not new ones.
let todMin=780, todOn=true, raining=false, clouding=false;
let cloudSpeed=1, cloudSize=1, cloudAlpha=0.72, cloudN=3, cloudStyle='shipped';
const litCache={key:'',cv:null}, skyCache={};

function daylight(h){                       // main.cpp:2809
  if(h>=8&&h<17)return 1;
  if(h>=6&&h<8)return (h-6)/2;
  if(h>=17&&h<20)return 1-(h-17)/3;
  return 0}
const nightAmount=h=>1-daylight(h);

// sky and the hills beyond the glass: blue-dominant, or the daylit green of the field
function skyMask(name){
  if(skyCache[name])return skyCache[name];
  const im=imgs['room|'+name]; if(!im||!im.complete||!im.naturalWidth)return null;
  const c=document.createElement('canvas'); c.width=GAME.w; c.height=GAME.h;
  const g=c.getContext('2d'); g.imageSmoothingEnabled=false;
  try{ g.drawImage(im,0,0,GAME.w,GAME.h) }catch(e){ return null }
  let d; try{ d=g.getImageData(0,0,GAME.w,GAME.h).data }catch(e){ return null }
  const m=new Uint8Array(GAME.w*GAME.h);
  for(let i=0,px=0;px<GAME.w*GAME.h;px++,i+=4){
    const R=d[i],G=d[i+1],B=d[i+2];
    if(B>R+18&&B>120)m[px]=1;
    else if(G>R+12&&G>B+8&&G>90)m[px]=2;
  }
  skyCache[name]=m; return m}

function skyBounds(name){
  const m=skyMask(name); if(!m)return null;
  let x0=999,y0=999,x1=-1,y1=-1;
  for(let y=0;y<GAME.h;y++)for(let x=0;x<GAME.w;x++)
    if(m[y*GAME.w+x]===1){if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y}
  return x1<0?null:{x0,y0,x1,y1,m}}

// the room at this hour: night regrade indoors, the landscape going to night WITH the sky
function litRoom(name){
  const im=imgs['room|'+name];
  if(!im||!im.complete||!im.naturalWidth) return null;
  if(!todOn) return im;
  const h=todMin/60, night=nightAmount(h), day=daylight(h);
  // rain gloom: rainAmount * daylight * 0.14, so a shower dims a bright afternoon
  // and never stacks onto night (updateDimPalette)
  const dim=Math.min(0.78, night*0.60 + (raining?1:0)*day*0.14);
  const key=name+'|'+todMin+'|'+(raining?'R':'');
  if(litCache.key===key && litCache.cv) return litCache.cv;
  const cv=litCache.cv||document.createElement('canvas');
  cv.width=GAME.w; cv.height=GAME.h;
  const g=cv.getContext('2d'); g.imageSmoothingEnabled=false;
  g.clearRect(0,0,GAME.w,GAME.h); g.drawImage(im,0,0,GAME.w,GAME.h);
  litCache.cv=cv; litCache.key=key;
  if(dim<0.004) return cv;                    // full daylight: nothing to do
  const k=1-dim, mask=skyMask(name);
  let img; try{ img=g.getImageData(0,0,GAME.w,GAME.h) }catch(e){ return cv }
  const d=img.data;
  for(let y=0;y<GAME.h;y++)for(let x=0;x<GAME.w;x++){
    const i=(y*GAME.w+x)*4, out=mask?mask[y*GAME.w+x]:0;
    let R=d[i],G=d[i+1],B=d[i+2];
    if(out){
      const sky=out===1;
      const nr=sky?16:24, ng=sky?20:28, nb=sky?90:74;
      const amt=night*(sky?1:0.88);
      R+=(nr-R)*amt; G+=(ng-G)*amt; B+=(nb-B)*amt;
    }else{
      // indoors: warm dark - red held up, blue pulled down, never a blue regrade
      R*=k+0.06*(1-k); G*=k; B*=k-0.04*(1-k);
    }
    d[i]=R; d[i+1]=G; d[i+2]=B;
  }
  g.putImageData(img,0,0);
  return cv}

// The firmware ships three clouds, {14x4 sp7} {10x3 sp9.5} {18x5 sp5}. The other styles are
// the same three shapes re-proportioned, keeping the firmware's drift speeds.
const CLOUD_STYLES={
  shipped:[{w:14,h:4,sp:7},{w:10,h:3,sp:9.5},{w:18,h:5,sp:5}],
  wisps:  [{w:26,h:2,sp:9},{w:20,h:2,sp:11},{w:32,h:3,sp:7}],
  puffs:  [{w:9,h:6,sp:6},{w:7,h:5,sp:8},{w:12,h:7,sp:5}],
  bank:   [{w:40,h:6,sp:4},{w:34,h:5,sp:5},{w:46,h:7,sp:3}],
  scatter:[{w:6,h:3,sp:11},{w:5,h:2,sp:13},{w:8,h:3,sp:9}]};
const DROPS=[], CLOUDS=[];
let skyBox=null;
function rebuildClouds(){
  const base=CLOUD_STYLES[cloudStyle]||CLOUD_STYLES.shipped;
  CLOUDS.length=0;
  for(let i=0;i<cloudN;i++){const b=base[i%3];
    CLOUDS.push({x:0,y:0,w:Math.max(2,Math.round(b.w*cloudSize)),
                 h:Math.max(1,Math.round(b.h*cloudSize)),sp:b.sp})}}
rebuildClouds();

function weatherInit(name){
  skyBox=skyBounds(name); if(!skyBox)return;
  skyBox._n=name;                 // tag it here, or the next step re-seeds everything
  const {x0,y0,x1,y1}=skyBox;
  DROPS.length=0;
  for(let i=0;i<18;i++)DROPS.push({x:x0+Math.random()*(x1-x0),y:y0+Math.random()*(y1-y0),
    sp:26+Math.random()*18});
  for(const c of CLOUDS){c.x=x0+Math.random()*(x1-x0);c.y=y0+2+Math.random()*Math.max(1,(y1-y0)/2)}}

function weatherStep(dt,name){
  if(!skyBox||skyBox._n!==name){weatherInit(name);if(skyBox)skyBox._n=name}
  if(!skyBox)return; const {x0,y0,x1,y1}=skyBox;
  if(raining)for(const d of DROPS){
    d.y+=d.sp*dt; d.x+=d.sp*0.22*dt;
    if(d.y>y1||d.x>x1){d.x=x0+Math.random()*(x1-x0);d.y=y0}}
  if(clouding)for(const c of CLOUDS){
    c.x+=c.sp*cloudSpeed*dt;
    if(c.x-c.w>x1){c.x=x0-c.w;c.y=y0+2+Math.random()*Math.max(1,(y1-y0)/2)}}}

// both paint ONLY on sky pixels, so they stay inside the window exactly as the device keeps them
function drawWeather(g,name,Zg){
  if(!skyBox||(!raining&&!clouding))return;
  const m=skyBox.m, W=GAME.w;
  if(clouding)for(const c of CLOUDS){
    g.fillStyle='rgba(255,255,255,'+cloudAlpha+')';
    for(let yy=0;yy<c.h;yy++)for(let xx=0;xx<c.w;xx++){
      const px=Math.round(c.x+xx),py=Math.round(c.y+yy);
      if(px<0||px>=GAME.w||py<0||py>=GAME.h)continue;
      if(m[py*W+px]!==1)continue;                 // clipped to the sky, i.e. to the window
      const taper=(xx<2||xx>c.w-3)&&(yy===0||yy===c.h-1);
      if(!taper)g.fillRect(px*Zg,py*Zg,Zg,Zg)}}
  if(raining)for(const d of DROPS){
    const px=Math.round(d.x),py=Math.round(d.y);
    if(px<0||px>=GAME.w||py<0||py>=GAME.h)continue;
    if(m[py*W+px]!==1)continue;                   // rain does not fall through the room
    g.fillStyle='rgba(150,190,240,0.85)';g.fillRect(px*Zg,py*Zg,Zg,Zg*2)}}

let wLoop=false,wLast=0;
function startWeatherLoop(){
  if(wLoop)return; wLoop=true; wLast=performance.now();
  (function tick(){
    if(!raining&&!clouding){wLoop=false;render();return}
    const now=performance.now();
    weatherStep(Math.min(0.05,(now-wLast)/1000),gRoom); wLast=now;
    if(!timer)render();
    requestAnimationFrame(tick)})()}
const hhmm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
"""

rep("function rpSize(p){", LIGHTING + "\nfunction rpSize(p){", "lighting + weather block")

# the scene painter uses the lit room and paints weather into the window
rep("""  const rm=imgs['room|'+gRoom];
  if(rm&&rm.complete) g.drawImage(rm,0,0,GAME.w*Zg,GAME.h*Zg);""",
    """  const rm=litRoom(gRoom)||imgs['room|'+gRoom];
  if(rm&&(rm.complete===undefined||rm.complete)) g.drawImage(rm,0,0,GAME.w*Zg,GAME.h*Zg);
  drawWeather(g,gRoom,Zg);            // inside the window, behind everything in the room""",
    "paintScene uses litRoom + weather")

# controls
rep("""      <div class="row" style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <select id="rpAdd"></select>""",
    """      <div style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <label>time of day <span class="val" id="todl">13:00</span>
          <button class="mini" id="todOff" style="float:right">lighting: on</button></label>
        <input type="range" id="tod" min="0" max="1439" step="5" value="780">
        <div class="row" style="margin-top:8px">
          <button class="mini" id="rn">rain</button>
          <button class="mini" id="cl">clouds</button>
          <select id="cst" style="flex:1">
            <option value="shipped" selected>shipped (3 sizes)</option>
            <option value="wisps">wisps</option>
            <option value="puffs">puffs</option>
            <option value="bank">bank</option>
            <option value="scatter">scatter</option>
          </select>
        </div>
        <div class="row" style="margin-top:6px">
          <div><label>n <span class="val" id="cnl">3</span></label>
            <input type="range" id="cn" min="0" max="10" step="1" value="3"></div>
          <div><label>size <span class="val" id="cwl">100%</span></label>
            <input type="range" id="cw" min="30" max="300" step="5" value="100"></div>
        </div>
        <div class="row" style="margin-top:6px">
          <div><label>speed <span class="val" id="csl">100%</span></label>
            <input type="range" id="cs" min="10" max="300" step="10" value="100"></div>
          <div><label>opacity <span class="val" id="col">72%</span></label>
            <input type="range" id="co" min="10" max="100" step="2" value="72"></div>
        </div>
        <div class="hint">the firmware ships 3 clouds at 100% / 100% / 72%</div>
      </div>

      <div class="row" style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <select id="rpAdd"></select>""",
    "lighting & weather controls")

rep("$('gscale').oninput=e=>{ gScale=+e.target.value; render() };",
    """const wxTouch=()=>{ litCache.key=''; if(raining||clouding) startWeatherLoop(); render() };
$('tod').oninput=e=>{ todMin=+e.target.value; $('todl').textContent=hhmm(todMin);
  litCache.key=''; render() };
$('todOff').onclick=()=>{ todOn=!todOn; litCache.key='';
  $('todOff').textContent='lighting: '+(todOn?'on':'off'); render() };
$('rn').onclick=()=>{ raining=!raining; if(raining)weatherInit(gRoom);
  $('rn').classList.toggle('pri',raining); wxTouch() };
$('cl').onclick=()=>{ clouding=!clouding; if(clouding)weatherInit(gRoom);
  $('cl').classList.toggle('pri',clouding); wxTouch() };
$('cst').onchange=e=>{ cloudStyle=e.target.value; rebuildClouds(); weatherInit(gRoom); wxTouch() };
$('cn').oninput=e=>{ cloudN=+e.target.value; $('cnl').textContent=cloudN;
  rebuildClouds(); weatherInit(gRoom); wxTouch() };
$('cw').oninput=e=>{ cloudSize=+e.target.value/100; $('cwl').textContent=e.target.value+'%';
  rebuildClouds(); weatherInit(gRoom); wxTouch() };
$('cs').oninput=e=>{ cloudSpeed=+e.target.value/100; $('csl').textContent=e.target.value+'%'; wxTouch() };
$('co').oninput=e=>{ cloudAlpha=+e.target.value/100; $('col').textContent=e.target.value+'%'; wxTouch() };
$('gscale').oninput=e=>{ gScale=+e.target.value; render() };""",
    "lighting & weather wiring")

# changing room invalidates the caches
rep("$('room').onchange=e=>{ gRoom=e.target.value;",
    "$('room').onchange=e=>{ gRoom=e.target.value; litCache.key=''; skyBox=null;",
    "reset caches on room change")

# the export records the hour and the weather
rep("    scene:{room:gRoom||null, width:GAME.w, height:GAME.h, floor:GAME.floor,",
    """    scene:{room:gRoom||null, width:GAME.w, height:GAME.h, floor:GAME.floor,
           light:{timeOfDay:hhmm(todMin), minutes:todMin, on:todOn,
                  daylight:+daylight(todMin/60).toFixed(3),
                  night:+nightAmount(todMin/60).toFixed(3)},
           weather:{rain:raining, clouds:clouding, style:cloudStyle,
                    count:cloudN, size:cloudSize, speed:cloudSpeed, opacity:cloudAlpha},""",
    "light + weather in the baked export")

# and the project remembers them
rep("  {clip,fps,character:{...CH},room:gRoom,roomObjects:rprops.map(p=>({...p})),",
    """  {clip,fps,character:{...CH},room:gRoom,roomObjects:rprops.map(p=>({...p})),
   light:{todMin,todOn},
   weather:{raining,clouding,cloudStyle,cloudN,cloudSize,cloudSpeed,cloudAlpha},""",
    "save light + weather")

rep("    if(p.room&&DATA.rooms&&DATA.rooms[p.room]){ gRoom=p.room; $('room').value=gRoom }",
    """    if(p.room&&DATA.rooms&&DATA.rooms[p.room]){ gRoom=p.room; $('room').value=gRoom }
    if(p.light){ todMin=+p.light.todMin||780; todOn=p.light.todOn!==false;
      $('tod').value=todMin; $('todl').textContent=hhmm(todMin);
      $('todOff').textContent='lighting: '+(todOn?'on':'off') }
    if(p.weather){ const w=p.weather;
      raining=!!w.raining; clouding=!!w.clouding;
      cloudStyle=w.cloudStyle||'shipped'; cloudN=w.cloudN==null?3:+w.cloudN;
      cloudSize=w.cloudSize==null?1:+w.cloudSize; cloudSpeed=w.cloudSpeed==null?1:+w.cloudSpeed;
      cloudAlpha=w.cloudAlpha==null?0.72:+w.cloudAlpha;
      $('cst').value=cloudStyle; $('cn').value=cloudN; $('cnl').textContent=cloudN;
      $('cw').value=Math.round(cloudSize*100); $('cwl').textContent=Math.round(cloudSize*100)+'%';
      $('cs').value=Math.round(cloudSpeed*100); $('csl').textContent=Math.round(cloudSpeed*100)+'%';
      $('co').value=Math.round(cloudAlpha*100); $('col').textContent=Math.round(cloudAlpha*100)+'%';
      $('rn').classList.toggle('pri',raining); $('cl').classList.toggle('pri',clouding);
      rebuildClouds(); if(raining||clouding){ weatherInit(gRoom); startWeatherLoop() } }
    litCache.key='';""",
    "load light + weather")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written:", len(s), "bytes (was", len(orig), ")")
