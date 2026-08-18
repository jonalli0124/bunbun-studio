"""Designated lighting: any room object can be marked as a light source.

Ported from the scene builder, which is the lighting record. The hard-won parts are the
comments' subject: where the light LEAVES the object (a wall sconce throws from below its
mount, a standing lamp from its shade), which way it throws (toward the middle of the room,
never tied to the art's flip flag), and that the floor pool lies FLAT rather than rotating
with the beam.
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


LAMPS = r"""
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
"""

rep("// ---------------- lighting & weather", LAMPS + "\n// ---------------- lighting & weather",
    "lamp block") if "// ---------------- lighting & weather" in s else rep(
    "function rpSize(p){", LAMPS + "\nfunction rpSize(p){", "lamp block")

# litRoom gains the lamp pass, and its cache must know about the lamps
rep("""  const key=name+'|'+todMin+'|'+(raining?'R':'');""",
    """  const lamp=lampNow(), gs=lampGeoms();
  const key=name+'|'+todMin+'|'+(raining?'R':'')+'|'+lampMode+'|'+lightScale+'|'
    +gs.map(q=>[Math.round(q.sx),Math.round(q.sy),Math.round(q.px),Math.round(q.py),
                Math.round(q.rx),q.power].join(':')).join(',');""",
    "lamp-aware cache key")

rep("  if(dim<0.004) return cv;                    // full daylight: nothing to do",
    "  if(dim<0.004 && (lamp<0.004||!gs.length)) return cv;   // full daylight, lamps out",
    "skip only when there is nothing to do")

rep("""    }else{
      // indoors: warm dark - red held up, blue pulled down, never a blue regrade
      R*=k+0.06*(1-k); G*=k; B*=k-0.04*(1-k);
    }
    d[i]=R; d[i+1]=G; d[i+2]=B;""",
    """    }else{
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
    d[i]=R; d[i+1]=G; d[i+2]=B;""",
    "the lamp pass")

# controls on the selected room object
rep("""          <div style="flex:0 0 auto;align-self:end">
            <button class="mini" id="rpFlip">flip &harr;</button>""",
    """          <div style="flex:0 0 auto;align-self:end">
            <button class="mini" id="rpLamp" title="make this object a light source">make light</button>
            <button class="mini" id="rpFlip">flip &harr;</button>""",
    "make light button")

rep("""        <div class="hint" id="rpWhere"></div>
      </div>""",
    """        <div class="hint" id="rpWhere"></div>
        <div id="rpLampBox" style="display:none;margin-top:8px;padding-top:8px;
             border-top:1px solid var(--line)">
          <div class="row">
            <div><label>brightness <span class="val" id="rplpv">100</span>%</label>
              <input type="range" id="rpPower" min="10" max="300" step="5" value="100"></div>
            <div><label>spread <span class="val" id="rplsv">100</span>%</label>
              <input type="range" id="rpSpread" min="20" max="300" step="5" value="100"></div>
          </div>
          <div class="row" style="margin-top:6px">
            <div><label>bulb height <span class="val" id="rpbhv">auto</span></label>
              <input type="range" id="rpBulb" min="-40" max="140" step="2" value="0"></div>
            <div style="flex:0 0 auto;align-self:end">
              <button class="mini" id="rpBulbAuto">auto</button></div>
          </div>
          <div class="hint">turn it with the object's own rotation &mdash; a sconce on its side
            washes the wall instead of the floor</div>
        </div>
      </div>""",
    "lamp controls")

rep("""        <div class="row" style="margin-top:8px">
          <button class="mini" id="rn">rain</button>""",
    """        <div class="row" style="margin-top:8px">
          <button class="mini" id="lampM">lamps: auto</button>
          <div><label>light size <span class="val" id="lszl">100%</span></label>
            <input type="range" id="lsz" min="30" max="220" step="5" value="100"></div>
        </div>
        <div class="row" style="margin-top:8px">
          <button class="mini" id="rn">rain</button>""",
    "lamp mode + light size")

rep("$('rpFlip').onclick=()=>{ const p=rprops[rsel]; if(p){p.flip=!p.flip; render()} };",
    """$('rpFlip').onclick=()=>{ const p=rprops[rsel]; if(p){p.flip=!p.flip; render()} };
$('rpLamp').onclick=()=>{ const p=rprops[rsel]; if(!p)return;
  p.lamp=!p.lamp; if(p.lamp){ p.lampPower=p.lampPower||1; p.lampSize=p.lampSize||1 }
  litCache.key=''; render() };
$('rpPower').oninput=e=>{ const p=rprops[rsel]; if(p){p.lampPower=+e.target.value/100;
  litCache.key=''; render()} };
$('rpSpread').oninput=e=>{ const p=rprops[rsel]; if(p){p.lampSize=+e.target.value/100;
  litCache.key=''; render()} };
$('rpBulb').oninput=e=>{ const p=rprops[rsel]; if(p){p.bulbY=+e.target.value;
  litCache.key=''; render()} };
$('rpBulbAuto').onclick=()=>{ const p=rprops[rsel]; if(p){delete p.bulbY;
  litCache.key=''; render()} };
$('lampM').onclick=()=>{ lampMode = lampMode==='auto'?'on':lampMode==='on'?'off':'auto';
  $('lampM').textContent='lamps: '+lampMode; litCache.key=''; render() };
$('lsz').oninput=e=>{ lightScale=+e.target.value/100; $('lszl').textContent=e.target.value+'%';
  litCache.key=''; render() };""",
    "lamp wiring")

rep("""    $('rpWhere').textContent=`at ${Math.round(p.x)},${Math.round(p.y)} `+""",
    """    $('rpLamp').classList.toggle('pri',!!p.lamp);
    $('rpLamp').textContent=p.lamp?'light \\u2713':'make light';
    $('rpLampBox').style.display=p.lamp?'':'none';
    if(p.lamp){ $('rplpv').textContent=Math.round((p.lampPower||1)*100);
      $('rplsv').textContent=Math.round((p.lampSize||1)*100);
      $('rpPower').value=Math.round((p.lampPower||1)*100);
      $('rpSpread').value=Math.round((p.lampSize||1)*100);
      $('rpbhv').textContent=(typeof p.bulbY==='number')?p.bulbY:'auto';
      $('rpBulb').value=(typeof p.bulbY==='number')?p.bulbY:bulbHeightOf(p); }
    $('rpWhere').textContent=`at ${Math.round(p.x)},${Math.round(p.y)} `+""",
    "lamp readouts")

# lights belong in the export
rep("""           weather:{rain:raining, clouds:clouding, style:cloudStyle,""",
    """           lamps:{mode:lampMode, level:+lampNow().toFixed(3), sizeScale:lightScale,
                  sources:lampItems().map(p=>{const gm=geomFor(p);
                    return {object:p.prop, x:p.x, y:p.y,
                      power:p.lampPower||1, spread:p.lampSize||1,
                      bulbY:(typeof p.bulbY==='number')?p.bulbY:bulbHeightOf(p),
                      bulb:{x:Math.round(gm.sx), y:Math.round(gm.sy)},
                      lands:gm.hitsFloor?{x:Math.round(gm.px), y:Math.round(gm.py)}:null}})},
           weather:{rain:raining, clouds:clouding, style:cloudStyle,""",
    "lamps in the baked export")

rep("   light:{todMin,todOn},",
    "   light:{todMin,todOn,lampMode,lightScale},",
    "lamp mode in the project")
rep("""    if(p.light){ todMin=+p.light.todMin||780; todOn=p.light.todOn!==false;""",
    """    if(p.light){ todMin=+p.light.todMin||780; todOn=p.light.todOn!==false;
      lampMode=p.light.lampMode||'auto'; lightScale=p.light.lightScale==null?1:+p.light.lightScale;
      $('lampM').textContent='lamps: '+lampMode;
      $('lsz').value=Math.round(lightScale*100); $('lszl').textContent=Math.round(lightScale*100)+'%';""",
    "load lamp mode")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written:", len(s), "bytes (was", len(orig), ")")
