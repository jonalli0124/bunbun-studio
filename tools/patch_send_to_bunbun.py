"""SEND TO BUNBUN: the whole port as one button on the Scene Assembler.

Jon: "is there a way from the export to create a bin / package file that they just upload
from the webpage?" then "i want there to be a button on the scene creation that does this".

No new format and no PC in the loop: the browser fetches the unit's CURRENT pak (BUNP v2,
byte-exact spec from convert_assets.ps1), bakes each animation's composed frames at the
pak's native 96px, encodes them with the SAME trim/RLE/RGB565 math (SAT 1.30, WHITE 246,
round-to-nearest), splices them in as canim/<slug>/<i> (plus any object art the pak lacks,
as items/<name>), POSTs the merged pak back to /api/ota/assets, waits out the reboot, then
uploads the device scene.json (props/blocks/bounds/floor/bun/anims/env/lamp - scene.h's own
shape) and restarts. The pet's age and phase are read before and after; a port must never
move them.

Firmware side (same evening): GET /api/ota/assets streams the pak back; CORS on the five
endpoints; scene.h reads the anims table. A page that cannot reach the device (an https
artifact cannot fetch http) says so and points at Export everything + the port command.
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


# ---------------------------------------------------------------- the button
rep("""      <button id="freshScene" title="empty room, nothing placed - save first if you want this one back">&#10024; start fresh</button>""",
    """      <button id="freshScene" title="empty room, nothing placed - save first if you want this one back">&#10024; start fresh</button>
      <button id="sendDev" class="pri" title="the whole port: art into the unit's pak, the scene onto its filesystem, the pet checked before and after">&#128007; Send to bunbun</button>
      <input id="devIp" placeholder="device IP" style="flex:0 0 110px;padding:6px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink);font:inherit;font-size:12px">""",
    "the button and the IP box")

# ---------------------------------------------------------------- bakeStep learns 1x
rep("""async function bakeStep(a, s2){
  const K=2, cvB=document.createElement('canvas'); cvB.width=96*K; cvB.height=96*K;""",
    """async function bakeStep(a, s2, opts){
  const K=(opts&&opts.K)||2, cvB=document.createElement('canvas'); cvB.width=96*K; cvB.height=96*K;""",
    "bakeStep takes a scale")

rep("""  const blob=await new Promise(r=>cvB.toBlob(r,'image/png'));
  return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
}
async function pngBytes(url){""",
    """  if(opts&&opts.canvas) return cvB;
  const blob=await new Promise(r=>cvB.toBlob(r,'image/png'));
  return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
}
async function pngBytes(url){""",
    "and can hand back the canvas")

# ---------------------------------------------------------------- the port itself
rep("""// ---- Export everything: one zip of the whole scene ----""",
    """// ---- SEND TO BUNBUN: the whole port, from this page, no PC tooling in the loop ----
// The pak format is BUNP v2 exactly as convert_assets.ps1 writes it - byte-for-byte the
// same trim, RLE and colour math, verified against the spec in that file's header. The
// device consumes only what it already understands.
const PakLib=(()=>{
  const SAT=1.30, WHITE=246.0;
  const clamp255=v=>v<0?0:(v>255?255:Math.round(v));
  function to565(r,g,b){
    const luma=0.299*r+0.587*g+0.114*b, w=255.0/WHITE;
    const nr=clamp255((luma+(r-luma)*SAT)*w), ng=clamp255((luma+(g-luma)*SAT)*w),
          nb=clamp255((luma+(b-luma)*SAT)*w);
    return ((Math.floor((nr*31+127)/255)<<11)|(Math.floor((ng*63+127)/255)<<5)
           | Math.floor((nb*31+127)/255))&0xFFFF;
  }
  function parse(buf){
    const dv=new DataView(buf), u8=new Uint8Array(buf);
    if(u8[0]!==0x42||u8[1]!==0x55||u8[2]!==0x4E||u8[3]!==0x50) throw new Error('not a BUNP pak');
    const ver=dv.getUint16(4,true), count=dv.getUint16(6,true);
    const entries=[];
    for(let i=0;i<count;i++){
      const at=8+i*56; let name='';
      for(let j=0;j<32;j++){ const c=u8[at+j]; if(!c) break; name+=String.fromCharCode(c); }
      entries.push({name, off:dv.getUint32(at+32,true), size:dv.getUint32(at+36,true),
        origW:dv.getUint16(at+40,true), origH:dv.getUint16(at+42,true),
        w:dv.getUint16(at+44,true), h:dv.getUint16(at+46,true),
        offX:dv.getInt16(at+48,true), offY:dv.getInt16(at+50,true), fmt:u8[at+52],
        data:u8.subarray(dv.getUint32(at+32,true), dv.getUint32(at+32,true)+dv.getUint32(at+36,true))});
    }
    return {ver, entries};
  }
  // one sprite: trim to the opaque box, RLE each row - convert_assets.ps1's Convert(), ported
  function encodeSprite(img){        // ImageData
    const W=img.width, H=img.height, px=img.data;
    let minX=W,minY=H,maxX=-1,maxY=-1;
    for(let y=0;y<H;y++) for(let x=0;x<W;x++)
      if(px[(y*W+x)*4+3]>=128){ if(x<minX)minX=x; if(x>maxX)maxX=x; if(y<minY)minY=y; if(y>maxY)maxY=y; }
    if(maxX<0) return {origW:W,origH:H,w:0,h:0,offX:0,offY:0,fmt:0,data:new Uint8Array(0)};
    const w=maxX-minX+1, h=maxY-minY+1, rows=[];
    for(let y=minY;y<=maxY;y++){
      // runs of opaque pixels, then split so skip and len both fit a u8 (the reader's
      // contract: per seg, x += skip; copy len; x += len) - 96px art never trips the split
      const segs=[]; let x=minX, last=0;
      while(x<=maxX){
        while(x<=maxX && px[(y*W+x)*4+3]<128) x++;
        if(x>maxX) break;
        let run=x; while(run<=maxX && px[(y*W+run)*4+3]>=128) run++;
        let skip=(x-minX)-last, at=x-minX, left=run-x;
        while(skip>255){ segs.push({skip:255,len:0,at:0,y}); skip-=255; }
        while(left>0){ const take=Math.min(255,left);
          segs.push({skip,len:take,at,y}); skip=0; at+=take; left-=take; }
        last=(run-minX); x=run;
      }
      rows.push(segs);
    }
    let bytes=0;
    for(const segs of rows){ bytes+=1; for(const g2 of segs) bytes+=2+g2.len*2; }
    const data=new Uint8Array(bytes); let p=0;
    for(const segs of rows){
      data[p++]=segs.length;
      for(const g2 of segs){
        data[p++]=g2.skip; data[p++]=g2.len;
        for(let k=0;k<g2.len;k++){
          const i=((g2.y*W)+(minX+g2.at+k))*4;
          const c=to565(px[i],px[i+1],px[i+2]);
          data[p++]=c&255; data[p++]=(c>>8)&255;
        }
      }
    }
    return {origW:W,origH:H,w,h,offX:minX,offY:minY,fmt:0,data};
  }
  function build(entries){
    const HEAD=8, E=56; let off=HEAD+entries.length*E;
    let total=off; for(const e of entries) total+=e.data.length;
    const buf=new Uint8Array(total), dv=new DataView(buf.buffer);
    buf[0]=0x42;buf[1]=0x55;buf[2]=0x4E;buf[3]=0x50;
    dv.setUint16(4,2,true); dv.setUint16(6,entries.length,true);
    entries.forEach((e,i)=>{
      const at=HEAD+i*E;
      for(let j=0;j<Math.min(31,e.name.length);j++) buf[at+j]=e.name.charCodeAt(j);
      dv.setUint32(at+32,off,true); dv.setUint32(at+36,e.data.length,true);
      dv.setUint16(at+40,e.origW,true); dv.setUint16(at+42,e.origH,true);
      dv.setUint16(at+44,e.w,true); dv.setUint16(at+46,e.h,true);
      dv.setInt16(at+48,e.offX,true); dv.setInt16(at+50,e.offY,true);
      buf[at+52]=e.fmt;
      buf.set(e.data,off); off+=e.data.length;
    });
    return buf;
  }
  return {parse, encodeSprite, build};
})();

// slug + key, the same rules the PC port uses: canim/<slug> fits the pak's 31, c_<slug>
// fits the mark's 15
function animSlug(name, taken){
  let s=name.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-+|-+$/g,'').slice(0,13)||'anim';
  let base=s,n=2; while(taken.has(s)){ s=(base.slice(0,11))+n; n++; }
  taken.add(s); return s;
}
function imageDataOf(imgOrCanvas, w, h){
  const c=document.createElement('canvas'); c.width=w; c.height=h;
  const g=c.getContext('2d',{willReadFrequently:true}); g.imageSmoothingEnabled=false;
  g.drawImage(imgOrCanvas,0,0,w,h);
  return g.getImageData(0,0,w,h);
}
// the walkable polygon's inscribed rectangle - the row-intersection over its own lanes
function inscribedBounds(pts){
  if(!pts||pts.length<3) return null;
  const ys=pts.map(p=>p[1]);
  const y0=Math.max(0,Math.round(Math.min(...ys))+2), y1=Math.min(239,Math.round(Math.max(...ys))-1);
  let x0=-1e9, x1=1e9;
  for(let y=y0;y<=y1;y++){
    const xs=[];
    for(let i=0,j=pts.length-1;i<pts.length;j=i++){
      const yi=pts[i][1], yj=pts[j][1];
      if((yi>y)===(yj>y)) continue;
      xs.push(pts[i][0]+(pts[j][0]-pts[i][0])*(y-yi)/(yj-yi));
    }
    if(xs.length<2) continue;
    xs.sort((a,b)=>a-b);
    x0=Math.max(x0,xs[0]); x1=Math.min(x1,xs[xs.length-1]);
  }
  if(!(x1-x0>=40 && y1-y0>=8)) return null;
  return {x0:Math.round(x0), x1:Math.round(x1), y0:y0, y1:y1};
}
// the DEVICE scene.json - scene.h's own shape, resolved here exactly as scene_push resolves
// it, but from this page's OWN numbers (its art is 1:1 pngs, so no k/pad archaeology)
function deviceScene(animPlan){
  const out={name:((($('sceneName')&&$('sceneName').value)||scene.room||'scene').slice(0,20))};
  if(scene.room) out.room='rooms/room-'+scene.room;
  const live=scene.objects.filter(o=>!o.hidden);
  const props=[], lamps=[];
  for(const o of live.slice(0,24)){
    const d=SD.props[o.object]; if(!d) continue;
    const s=+(o.scale*passiveScale).toFixed(4);
    const p={n:pakNameFor(o.object), x:+o.x.toFixed(1), y:+(o.y-d.h*o.scale*passiveScale).toFixed(1),
             s:s, f:!!o.flip, z:(o.z==null?0:(o.z>o.y?1:0))};
    if(o.rot) p.r=o.rot;
    props.push(p);
    if(o.lamp){ const e={i:props.length-1};
      if(o.lampPower&&o.lampPower!==1) e.pw=Math.round(o.lampPower*100);
      if(o.lampSize&&o.lampSize!==1)  e.sz=Math.round(o.lampSize*100);
      if(typeof o.bulbY==='number') e.by=Math.round(o.bulbY);
      e.ay=+o.y.toFixed(1); lamps.push(e); }
  }
  out.props=props;
  // blocks: the biggest solid footprints, the same block the preview walks around
  const blocks=live.filter(o=>!o.walkOver&&SD.props[o.object])
    .map(o=>{ const d=SD.props[o.object], w=d.w*o.scale*passiveScale;
      return {x0:Math.round(o.x-w/2), x1:Math.round(o.x+w/2), yFront:Math.round(o.y), area:w}; })
    .sort((a,b)=>b.area-a.area).slice(0,8).map(b=>({x0:b.x0,x1:b.x1,yFront:b.yFront}));
  if(blocks.length) out.blocks=blocks;
  const fl=scene.zones.find(z=>z.kind==='floor'&&z.on!==false);
  if(fl){
    const b=inscribedBounds(fl.pts); if(b) out.bounds=b;
    const pts=fl.pts.length<=12?fl.pts:fl.pts.filter((_,i)=>i%Math.ceil(fl.pts.length/12)===0).slice(0,12);
    out.floor=pts.map(p=>({x:Math.round(p[0]),y:Math.round(p[1])}));
  }
  if(lamps.length) out.lamp=lamps.slice(0,4);
  // marks + the anims table, from the plan the pak merge already made
  if(animPlan.marks.length) out.bun=animPlan.marks.slice(0,6);
  if(animPlan.anims.length) out.anims=animPlan.anims.slice(0,8);
  const env={};
  if(clouding){ env.cn=Math.min(8,cloudN); env.cw=Math.round(cloudSize*100);
    env.cs=Math.round(cloudSpeed*100); env.ca=Math.round(cloudAlpha*100);
    env.ct=Math.max(0,['shipped','wisps','puffs','bank','scatter'].indexOf(cloudStyle)); }
  else env.cn=0;
  env.rw=raining?1:0;
  if(Math.round(lightScale*100)!==100) env.ls=Math.round(lightScale*100);
  out.env=env;
  return out;
}
function pakNameFor(objectName){
  const nm='items/'+objectName.split('/').pop();
  if(nm.length<=31) return nm;
  return nm.slice(0,26)+'-'+hash4(nm);       // pak_safe's shape; hash4 below
}
function hash4(str){ let h=0; for(let i=0;i<str.length;i++){ h=(h*31+str.charCodeAt(i))>>>0; }
  return ('0000'+h.toString(16)).slice(-4); }

async function sendToBunbun(){
  const ipBox=$('devIp'), ip=(ipBox.value||'').trim();
  try{ localStorage.setItem('bunbun-device-ip', ip) }catch(e){}
  if(!ip) return say('type the device IP first (the unit shows it on its SETUP screen)',true);
  if(location.protocol==='https:')
    return say('this page is on https and cannot talk to a home device — open the tool '+
               'from your local server, or use Export everything + the port command',true);
  const B=$('sendDev'), setB=t=>{B.textContent=t};
  B.disabled=true;
  try{
    // 0. the pet, before anything
    setB('checking the pet…');
    const info=await (await fetch(`http://${ip}/api/system/info`)).json();
    const pet0={age:info.info&&info.info.pet_age_min, phase:info.info&&info.info.pet_phase};
    say(`found the unit — pet age ${pet0.age} min, phase ${pet0.phase}. Nothing here may change that.`);
    // 1. its current pak
    setB('fetching its art pak…');
    const pakBuf=await (await fetch(`http://${ip}/api/ota/assets`)).arrayBuffer();
    const pak=PakLib.parse(pakBuf);
    say(`pak in hand: ${pak.entries.length} sprites`);
    // 2. bake the animations at 96px + encode; plan slugs, marks, the anims table
    const taken=new Set(), plan={anims:[], marks:[]}, newEntries=[];
    const on=anims.filter(a=>a.on);
    for(const a of on.slice(0,8)){
      setB(`baking "${a.name}"…`);
      await loadArtFor(a);
      const slug=animSlug(a.name, taken), key='c_'+slug;
      let n=0;
      for(let s2=0;s2<a.order.length;s2++){
        const cvB=await bakeStep(a, s2, {K:1, canvas:true});
        if(!cvB) continue;
        const e=PakLib.encodeSprite(cvB.getContext('2d').getImageData(0,0,96,96));
        e.name=`canim/${slug}/${n}`; newEntries.push(e); n++;
      }
      if(!n) continue;
      plan.anims.push({k:key, f:'canim/'+slug, n:n,
        fps:+Math.min(24,Math.max(0.5,(a.fps||7)/Math.max(1,a.character.hold||1))).toFixed(2), m:0});
      // where: the same rules the preview lives by
      const pl=a.place||{dx:0,dy:0}, spots=[];
      const p=placesFor(a);
      if(a.onItems&&a.onItems.length){
        for(const oid of a.onItems){ const o=scene.objects.find(x=>x.oid===oid);
          if(o) spots.push([o.x+pl.dx, o.y+pl.dy]); }
      } else if(a.inArea!=null){
        const z=scene.zones.find(z2=>z2.kind==='spot'&&z2.spotId===a.inArea);
        if(z){ const cx=z.pts.reduce((q,r)=>q+r[0],0)/z.pts.length,
               cy=z.pts.reduce((q,r)=>q+r[1],0)/z.pts.length;
          spots.push([cx+pl.dx, cy+pl.dy]); }
      } else if(a.rule&&a.rule.needs){
        for(const o of scene.objects) if(!o.hidden&&(o.can||[]).includes(a.rule.needs))
          spots.push([o.x+pl.dx, o.y+pl.dy]);
      } else {
        const fl=scene.zones.find(z=>z.kind==='floor');
        if(fl){ const cx=fl.pts.reduce((q,r)=>q+r[0],0)/fl.pts.length,
                cy=fl.pts.reduce((q,r)=>q+r[1],0)/fl.pts.length; spots.push([cx,cy]); }
      }
      for(const [sx,sy] of spots) if(plan.marks.length<6)
        plan.marks.push({x:Math.round(sx), y:Math.round(sy), a:key});
    }
    // 3. object art the pak does not have yet
    for(const o of scene.objects.filter(x=>!x.hidden)){
      const nm=pakNameFor(o.object), d=SD.props[o.object];
      if(!d||pak.entries.some(e=>e.name===nm)||newEntries.some(e=>e.name===nm)) continue;
      const im=imgs['prop|'+o.object]; if(!im) continue;
      const e=PakLib.encodeSprite(imageDataOf(im, d.w, d.h)); e.name=nm; newEntries.push(e);
    }
    // the room must already be in the pak - this button does not encode rooms (yet)
    if(scene.room && !pak.entries.some(e=>e.name==='rooms/room-'+scene.room))
      say(`heads up: the unit's pak has no "rooms/room-${scene.room}" — the room will fall `+
          `back until one port runs from the PC`,true);
    // 4. merge: everything the unit had, minus the canim folders being replaced, plus the new
    const replacing=new Set(plan.anims.map(a2=>a2.f+'/'));
    const kept=pak.entries.filter(e=>![...replacing].some(r=>e.name.startsWith(r)));
    const merged=PakLib.build(kept.concat(newEntries));
    say(`merged pak: ${kept.length} kept + ${newEntries.length} new = ${(merged.length/1024)|0} KB`);
    // 5. send the pak - the unit stops AirPlay, writes it, reboots itself
    setB('sending the pak…');
    const r1=await fetch(`http://${ip}/api/ota/assets`, {method:'POST', body:merged});
    if(!r1.ok) throw new Error('pak upload failed: '+r1.status);
    say('pak written — the unit is rebooting…');
    setB('waiting for reboot…');
    await new Promise(r=>setTimeout(r,9000));
    let back=false;
    for(let i=0;i<20&&!back;i++){
      try{ const c=new AbortController(); const t=setTimeout(()=>c.abort(),3000);
        await fetch(`http://${ip}/api/system/info`,{signal:c.signal}); clearTimeout(t); back=true; }
      catch(e){ await new Promise(r=>setTimeout(r,2500)); }
    }
    if(!back) throw new Error('the unit did not come back after the pak write');
    // 6. the scene itself, then one restart so it reads it
    setB('sending the scene…');
    const dev=deviceScene(plan);
    const body=JSON.stringify(dev);
    if(body.length>8192) throw new Error(`scene is ${body.length} bytes; the device reads 8192`);
    const r2=await fetch(`http://${ip}/api/fs/upload?path=/spiffs/scene.json`,
      {method:'POST', headers:{'Content-Type':'application/json'}, body});
    if(!r2.ok) throw new Error('scene upload failed: '+r2.status);
    await fetch(`http://${ip}/api/system/restart`,{method:'POST'}).catch(()=>{});
    setB('final restart…');
    await new Promise(r=>setTimeout(r,9000));
    let pet1=null;
    for(let i=0;i<20&&!pet1;i++){
      try{ const c=new AbortController(); const t=setTimeout(()=>c.abort(),3000);
        const j=await (await fetch(`http://${ip}/api/system/info`,{signal:c.signal})).json();
        clearTimeout(t); pet1={age:j.info&&j.info.pet_age_min, phase:j.info&&j.info.pet_phase}; }
      catch(e){ await new Promise(r=>setTimeout(r,2500)); }
    }
    if(!pet1) throw new Error('scene sent, but the unit has not come back to be checked');
    if(pet1.phase!==pet0.phase || (typeof pet1.age==='number'&&typeof pet0.age==='number'&&pet1.age<pet0.age-1))
      say(`!!! THE PET MOVED (was ${pet0.age}/${pet0.phase}, now ${pet1.age}/${pet1.phase}) — tell Jon before anything else`,true);
    else
      say(`PORTED ✔ ${plan.anims.length} animation(s), ${plan.marks.length} mark(s), `+
          `${dev.props.length} object(s) — pet unchanged (age ${pet1.age}, phase ${pet1.phase})`);
  }catch(err){
    say('send failed — '+err.message+' (Export everything + the port command still works)',true);
  }finally{
    B.disabled=false; B.textContent='🐇 Send to bunbun';
  }
}
$('sendDev').onclick=sendToBunbun;
try{ $('devIp').value=localStorage.getItem('bunbun-device-ip')||'' }catch(e){}

// ---- Export everything: one zip of the whole scene ----""",
    "the port")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
