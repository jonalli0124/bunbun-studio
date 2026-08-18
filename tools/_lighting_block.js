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
const smoothstep=(a,b,x)=>{const t=Math.max(0,Math.min(1,(x-a)/(b-a)));return t*t*(3-2*t)};

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
  const lamp=lampNow(), gs=lampGeoms();
  const key=name+'|'+todMin+'|'+(raining?'R':'')+'|'+lampMode+'|'+lightScale+'|'
    +gs.map(q=>[Math.round(q.sx),Math.round(q.sy),Math.round(q.px),Math.round(q.py),
                Math.round(q.rx),q.power].join(':')).join(',');
  if(litCache.key===key && litCache.cv) return litCache.cv;
  const cv=litCache.cv||document.createElement('canvas');
  cv.width=GAME.w; cv.height=GAME.h;
  const g=cv.getContext('2d'); g.imageSmoothingEnabled=false;
  g.clearRect(0,0,GAME.w,GAME.h); g.drawImage(im,0,0,GAME.w,GAME.h);
  litCache.cv=cv; litCache.key=key;
  if(dim<0.004 && (lamp<0.004||!gs.length)) return cv;   // full daylight, lamps out
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
      // A rain sky is overcast whatever the clock says. The builder's rain term is
      // rainAmount*daylight*0.14 and applies INDOORS only, so a shower left the window
      // bright blue behind its own drops. Pull the sky toward slate, on top of the hour.
      if(raining){
        const gr=sky?86:78, gg=sky?94:86, gb=sky?104:92;
        const w=0.45*(1-night*0.5);        // strongest by day, still present at dusk
        R+=(gr-R)*w; G+=(gg-G)*w; B+=(gb-B)*w;
      }
    }else{
      // indoors: warm dark - red held up, blue pulled down, never a blue regrade
      R*=k+0.06*(1-k); G*=k; B*=k-0.04*(1-k);
    }
    if(lamp>0.004&&!out&&gs.length){
      let acc=0;
      for(const L of gs){
        // measured ALONG the beam, not down the screen, so a turned lamp still works
        let inten=0;
        const ex=x-L.sx, ey=y-L.sy, axLen=Math.max(1,L.len);
        const u=(ex*L.ux+ey*L.uy)/axLen, v=Math.abs(-ex*L.uy+ey*L.ux);
        if(u>=0&&u<=1){
          const hw=L.half+(L.rx-L.half)*u, dd=v/Math.max(1,hw);
          if(dd<1){const along=u<0.88?1:1-0.45*((u-0.88)/0.12);
            inten=along*(1-smoothstep(0.72,1,dd))*0.62}}
        // the pool lies FLAT on the floor, never tilted with the beam
        if(L.hitsFloor){
          const ddx=(x-L.px)/L.rx, ddy=(y-L.py)/L.ry, rr=Math.sqrt(ddx*ddx+ddy*ddy);
          if(rr<1){const pp=1-smoothstep(0.82,1,rr); if(pp>inten)inten=pp}}
        acc+=inten*L.power}
      if(acc>0.004){const w=Math.min(1.6,acc)*lamp;   // lamps add, and can overlap brighter
        R=Math.min(255,R+92*w); G=Math.min(255,G+72*w); B=Math.min(255,B+34*w)}}
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
    if(now-wLast>=45){                       // ~22fps is plenty for drifting cloud
      weatherStep(Math.min(0.05,(now-wLast)/1000),gRoom); wLast=now;
      if(!timer)render();
    }
    requestAnimationFrame(tick)})()}
const hhmm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');

// ---------------- designated lighting, ported from the builder ----------------
let lampMode='auto', lightScale=1;
function lampLevel(h){                      // main.cpp:2817
  if(h>=18)return Math.min(1,(h-18)/0.25);
  if(h<6)return 1;
  if(h<6.25)return Math.max(0,1-(h-6)/0.25);
  return 0}
const lampNow=()=>{const h=todMin/60;
  return lampMode==='on'?1:lampMode==='off'?0:lampLevel(h)};
const lampItems=()=>rprops.filter(p=>p.lamp);

// MOUNTED OR STANDING, judged by where the thing sits - not by how big its sprite is.
// A fixture anchored high on the wall throws from below its mount (the firmware's sconce,
// -27); anything standing on the floor throws from its shade, near the top of the sprite.
function bulbHeightOf(fx){
  if(!fx)return -27;
  if(typeof fx.bulbY==='number')return fx.bulbY;
  const d=DATA.props[fx.prop];
  const h=d?d.h*fx.scale:0;
  if(fx.y<120)return -27;                 // up on the wall
  return Math.max(6,Math.round(h-6));     // standing: the shade, just under the top
}

function geomFor(fx){
  const ax=fx?fx.x:297, ay=fx?fx.y:29, rot=fx?(fx.rot||0):0;
  const pw=fx&&fx.lampPower?fx.lampPower:1;                 // brightness ONLY
  const sc=lightScale*(fx&&fx.lampSize?fx.lampSize:1);      // spread, kept separate
  const bulbUp=bulbHeightOf(fx);
  // A wall sconce throws INTO the room, so the default is "toward the middle". Flip is about
  // the ART - it says which way the fixture is drawn, not where the light goes.
  const mir=(ax>160)?1:-1;
  const standing=bulbUp>0;
  const sx=ax-(standing?0:3*mir), sy=ay-bulbUp;
  const floorY=210;                        // FLOOR_Y + 10
  const drop=Math.max(1,floorY-sy), ref=210-56;   // the firmware's own drop, as reference
  const t=drop/ref;
  // a sconce angles off its wall (the full 58 the firmware uses); a standing lamp mostly
  // pours straight down, so it keeps only a quarter of that lean
  const lean=standing?0.25:1;
  const vx0=-58*t*mir*lean, vy0=drop;
  const a=rot*Math.PI/180, ca=Math.cos(a), sa=Math.sin(a);
  let vx=vx0*ca-vy0*sa, vy=vx0*sa+vy0*ca;
  const L0=Math.max(1,Math.hypot(vx,vy)); let ux=vx/L0, uy=vy/L0;
  // The pool is where the axis MEETS THE FLOOR, and it lies FLAT on it. A beam angled up or
  // sideways never reaches the floor, so it gets no pool: it just washes whatever it crosses.
  let px,py,len,hitsFloor=false;
  const reach=(uy>0.001)?(floorY-sy)/uy:1e9;
  if(uy>0.15&&reach<=340){
    len=reach; hitsFloor=true; px=sx+ux*len; py=floorY;
  }else{
    len=Math.max(40,drop); px=sx+ux*len; py=sy+uy*len;
  }
  return {sx,sy,px,py,ux,uy,len,hitsFloor,
          rx:96*Math.max(0.45,t)*sc, ry:22*Math.max(0.5,t)*sc,
          half:12*sc, rot, mir, fx, scale:sc, power:pw}}
const lampGeoms=()=>{const L=lampItems(); return L.length?L.map(geomFor):[]};

