"""Preview: let the pet actually walk the room and use what it finds.

The ghosts show WHERE something may happen. This shows it happening - he wanders the walkable
shape, picks one of the places that affords what an animation needs, walks there, plays it, and
goes looking again. Two chairs both marked "sit" are two candidates; two sit animations on one
chair are also two candidates. He chooses among all of them, which is the only honest way to see
whether a scene reads the way a kid meant it to.

Walk speed and the pause between wanders are the shipped firmware's, not invented.
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


# ---------------------------------------------------------------- the character, drawn once
# The ghost's drawing code was trapped inside draw(); the preview needs the same body at a
# different place and opacity, so it becomes a function both call.
rep("""    const put=(x,y)=>{ const im=imgs['clip|'+a.clip+'|'+fi] || imgs['clip|'+a.clip+'|0'];""",
    """    const put=(x,y)=>drawChar(a,step,x,y,0.72,true);
    const _unusedPut=(x,y)=>{ const im=imgs['clip|'+a.clip+'|'+fi] || imgs['clip|'+a.clip+'|0'];""",
    "ghost calls the shared painter")

rep("""// WHERE an animation may actually happen in THIS room.""",
    """// One character, painted at a spot, at whatever step of its own order. Used by the ghosts and
// by the preview, so what you watch cannot drift from what you placed.
function drawChar(a, step, x, y, alpha, mark, clipOver){
  const clip = clipOver || a.clip;
  const sc=SCALE[phaseOf(clip)];
  const seq=[]; for(const o of a.order) for(let h=0;h<a.character.hold;h++) seq.push(o);
  const ent = clipOver ? {i:step % (SD.clips[clip]?SD.clips[clip].frames.length:1), cs:100}
                       : seq[((step%seq.length)+seq.length)%seq.length];
  const fi=ent.i, f=(SD.clips[clip]&&SD.clips[clip].frames[fi])||{};
  const per=(ent.cs==null?100:ent.cs)/100;
  const pulse=(!clipOver && a.character.breathe)
    ? (a.character.breathe/100)*Math.sin(2*Math.PI*step/Math.max(2,a.character.period)) : 0;
  const k=per*(1+pulse);
  const bb=f.bbox||[0,0,96,96];
  const bx=(bb[0]+bb[2])/2, by=a.character.pivot==='middle'?(bb[1]+bb[3])/2:bb[3];
  const im=imgs['clip|'+clip+'|'+fi] || imgs['clip|'+clip+'|0'];
  if(!im) return;
  const toRoom=(sx,sy)=>[x+(sx-48)*sc, y+(sy-90)*sc];
  ctx.save(); ctx.globalAlpha=alpha;
  if(Math.abs(k-1)>1e-6){ const c=toRoom(bx,by);
    ctx.translate(c[0]*Z,c[1]*Z); ctx.scale(k,k); ctx.translate(-c[0]*Z,-c[1]*Z); }
  ctx.drawImage(im,(x-48*sc)*Z,(y-90*sc)*Z,96*sc*Z,96*sc*Z);
  if(!clipOver) for(const L of a.layers){
    const d=SD.props[L.prop], pi=imgs['prop|'+L.prop];
    if(!d||!pi||L.on===false) continue;
    let pt = L.anchor==='head' ? f.head
           : L.anchor==='feet' ? (f.bbox?[(f.bbox[0]+f.bbox[2])/2,f.bbox[3]]:null)
           : f[L.anchor];
    if(!pt) continue;
    const off=L.off||{x:0,y:0};
    const jx=L.jig?Math.sin(step*2.4)*L.jig:0, jy=L.jig?Math.cos(step*3.1)*L.jig*0.6:0;
    const puff=L.pulse?1+(L.pulse/100)*Math.sin(2*Math.PI*step/Math.max(2,L.pper||8)):1;
    const [rx,ry]=toRoom(pt[0]+off.x+jx, pt[1]+off.y+jy);
    const w=d.w*(L.scale||1)*puff*sc, h=d.h*(L.scale||1)*puff*sc;
    ctx.save(); ctx.globalAlpha=alpha*((L.opac==null?100:L.opac)/100);
    ctx.translate(rx*Z,ry*Z);
    if(L.rot) ctx.rotate(L.rot*Math.PI/180);
    if(L.flip) ctx.scale(-1,1);
    ctx.drawImage(pi,-w/2*Z,-h/2*Z,w*Z,h*Z);
    ctx.restore();
  }
  ctx.restore();
  if(mark){ ctx.strokeStyle='#e8392d'; ctx.lineWidth=2; ctx.setLineDash([4,3]);
    ctx.beginPath(); ctx.ellipse(x*Z,y*Z,9*Z,3*Z,0,0,7); ctx.stroke(); ctx.setLineDash([]) }
}

// ---------------------------------------------------------------- the preview
// Every (animation, place) pair he could choose next. An animation that may happen anywhere is
// one candidate with no place; one that needs "sit" is one candidate PER object affording sit,
// so two chairs really are two destinations and two sit animations really are two things to do
// when he gets to one.
function candidates(){
  const out=[];
  for(const a of anims){ if(!a.on) continue;
    const p=placesFor(a);
    if(p.kind==='anywhere') out.push({a, spot:null});
    else for(const o of p.spots) out.push({a, spot:o});
  }
  return out;
}
const WALK_PXS=16;              // main.cpp wander speed
const REST_S=1.2;               // the beat before he sets off again
let sim=null, simOn=false;
function floorPoly(){ const z=scene.zones.find(q=>q.kind==='floor'&&q.pts&&q.pts.length>2);
  return z?z.pts:null }
function walkable(x,y){
  const fl=floorPoly();
  if(fl && !inPoly(x,y,fl)) return false;
  return !scene.zones.some(z=>z.kind==='keepout'&&z.pts&&z.pts.length>2&&inPoly(x,y,z.pts));
}
function someWalkablePoint(){
  const fl=floorPoly();
  if(!fl) return {x:GAME.w/2, y:GAME.floor};
  const xs=fl.pts?fl.pts.map(p=>p[0]):fl.map(p=>p[0]);
  const ys=fl.pts?fl.pts.map(p=>p[1]):fl.map(p=>p[1]);
  const x0=Math.min(...xs), x1=Math.max(...xs), y0=Math.min(...ys), y1=Math.max(...ys);
  for(let i=0;i<80;i++){ const x=x0+Math.random()*(x1-x0), y=y0+Math.random()*(y1-y0);
    if(walkable(x,y)) return {x,y} }
  return {x:(x0+x1)/2, y:(y0+y1)/2};
}
function walkClipFor(a){
  const want=phaseOf(a.clip)==='baby' ? /baby.*(walk|crawl)/i : /adult.*walk/i;
  return Object.keys(SD.clips).find(k=>want.test(k)) || null;
}
function chooseNext(){
  const cands=candidates();
  if(!cands.length){ sim=null; return }
  const c=cands[Math.floor(Math.random()*cands.length)];
  const to = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();
  const from = sim ? {x:sim.x, y:sim.y} : someWalkablePoint();
  sim={...(sim||{}), x:from.x, y:from.y, tx:to.x, ty:to.y,
       a:c.a, spot:c.spot, phase:'walk', t:0, plays:0,
       walkClip:walkClipFor(c.a), says:c.spot?`walking to the ${c.spot.object}`:'wandering'};
}
function stepSim(dt){
  if(!simOn) return;
  if(!sim){ chooseNext(); if(!sim) return }
  sim.t+=dt;
  if(sim.phase==='walk'){
    const dx=sim.tx-sim.x, dy=sim.ty-sim.y, d=Math.hypot(dx,dy);
    const step=WALK_PXS*dt;
    if(d<=step){ sim.x=sim.tx; sim.y=sim.ty; sim.phase='play'; sim.t=0;
      sim.says=(sim.spot?`${sim.a.name} at the ${sim.spot.object}`:sim.a.name); }
    else { sim.x+=dx/d*step; sim.y+=dy/d*step; sim.face=dx<0?-1:1 }
  } else if(sim.phase==='play'){
    const seq=sim.a.order.length*Math.max(1,sim.a.character.hold);
    const loops=Math.floor(sim.t*(sim.a.fps||7)/seq);
    if(loops>=2){ sim.phase='rest'; sim.t=0; sim.says='looking for something to do' }
  } else if(sim.t>REST_S) chooseNext();
}
function drawSim(){
  if(!simOn||!sim) return;
  if(sim.phase==='walk' && sim.walkClip){
    drawChar(sim.a, Math.floor(sim.t*8), sim.x, sim.y, 1, false, sim.walkClip);
  } else if(sim.phase==='walk'){
    drawChar(sim.a, 0, sim.x, sim.y, 1, false);
  } else {
    drawChar(sim.a, Math.floor(sim.t*(sim.a.fps||7)), sim.x, sim.y, 1, false);
  }
}

// WHERE an animation may actually happen in THIS room.""",
    "the shared painter and the sim")

# ---------------------------------------------------------------- wire it in
rep("""  // the selected animation, ghosted at every place it could happen
  const a=anims[asel];
  if(a && a.on){""",
    """  drawSim();

  // the selected animation, ghosted at every place it could happen
  const a=anims[asel];
  if(a && a.on && !simOn){""",
    "ghosts step aside for the preview")

rep("""      <button id="fromLib">From the editor</button>""",
    """      <button id="preview">Preview</button>
      <button id="fromLib">From the editor</button>""",
    "preview button")

rep("""$('room').innerHTML=Object.keys(SD.rooms).map""",
    """$('preview').onclick=()=>{ simOn=!simOn; sim=null;
  $('preview').classList.toggle('pri',simOn);
  $('preview').textContent=simOn?'Stop preview':'Preview';
  if(simOn){ const n=candidates().length;
    say(n ? `watching \\u2014 ${n} thing${n===1?'':'s'} he could choose`
          : 'nothing to watch \\u2014 turn an animation on, and mark something it can use');
    if(!playing) $('play').click(); }
  else say('preview stopped');
  draw() };
$('room').innerHTML=Object.keys(SD.rooms).map""",
    "preview toggle")

# the loop already ticks; feed the sim from it
rep("""  if(now-lastT>=55){ tick=now/1000;
    if(raining||clouding) weatherStep((now-lastT)/1000, scene.room);
    lastT=now; draw() }""",
    """  if(now-lastT>=55){ const dt=Math.min(0.25,(now-lastT)/1000); tick=now/1000;
    if(raining||clouding) weatherStep(dt, scene.room);
    const wasSaying=sim&&sim.says;
    stepSim(dt);
    if(simOn&&sim&&sim.says!==wasSaying) say(sim.says);
    lastT=now; draw() }""",
    "drive the sim from the loop")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
