"""Codes on everything, names on the canvas, and one list style for all three kinds.

Jon: "can we have a small code for each of the animations and put it above the animation and the
same concept for objects and areas ... we need to use the same list method as the animation for
objects and areas with all features like lock turn on or off" - "that way we can define the
relationship".

So every thing gets a lasting code, shown in its list row AND on the canvas:

    animations   A1, A2...   drawn just above the ghost
    objects      O1, O2...   (their oid) name + code centred on the sprite
    areas        area 1...   already centred; keep-outs and the floor get their names too

and the zones list becomes rows like the animations: on/off, lock, delete. A zone turned OFF
stops applying - a keep-out stops keeping out, an area stops being offered, the floor falls back
to the device band - which is how you A/B a layout without redrawing it.

Labels show while editing (ghosts on) and get out of the way when watching (sim, or Ghosts: off).
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


# ---------------------------------------------------------------- lasting codes
rep("""let spotSeq=1, oidSeq=1;""",
    """let spotSeq=1, oidSeq=1, acodeSeq=1;
// animations get A1, A2... - lasting, so the code on screen always means the same row
function ensureAcodes(){ for(const a of anims) if(a.acode==null){
  while(anims.some(q=>q.acode===acodeSeq)) acodeSeq++;
  a.acode=acodeSeq++; } }""",
    "animation codes")

# ---------------------------------------------------------------- canvas labels
rep("""      ctx.strokeStyle='#e8392d'; ctx.lineWidth=2; ctx.setLineDash([4,3]);
      ctx.beginPath(); ctx.ellipse(x*Z,y*Z,9*Z,3*Z,0,0,7); ctx.stroke(); ctx.setLineDash([]);
      // an outline round the selected one, so "which of these am I dragging" is never a guess""",
    """      ctx.strokeStyle='#e8392d'; ctx.lineWidth=2; ctx.setLineDash([4,3]);
      ctx.beginPath(); ctx.ellipse(x*Z,y*Z,9*Z,3*Z,0,0,7); ctx.stroke(); ctx.setLineDash([]);
      // its code, just above it - the same A-number its list row wears
      { const b=ghostBounds(a,{x,y});
        if(b){ ctx.font='700 '+(3.6*Z)+'px ui-sans-serif,system-ui'; ctx.textAlign='center';
          ctx.fillStyle='rgba(0,0,0,0.55)';
          ctx.fillText('A'+(a.acode||'?'), x*Z+1, (b.t-2)*Z+1);
          ctx.fillStyle='#fff';
          ctx.fillText('A'+(a.acode||'?'), x*Z, (b.t-2)*Z); ctx.textAlign='left' } }
      // an outline round the selected one, so "which of these am I dragging" is never a guess""",
    "the code floats above each ghost")

rep("""  for(const o of items){
    if(charZ!=null && !charDrawn && (o.z==null?o.y:o.z) > charZ) paintChar();
    if(o.hidden) continue;                       // kept in every way, simply not drawn""",
    """  const labelObj=o=>{ if(simOn||!showGhosts) return;   // editing aids, not scenery
    const {w,h}=sizeOf(o);
    ctx.font='700 '+(3.4*Z)+'px ui-sans-serif,system-ui'; ctx.textAlign='center';
    const t=nice(o.object)+' O'+(o.oid||'?');
    ctx.fillStyle='rgba(0,0,0,0.55)'; ctx.fillText(t, o.x*Z+1, (o.y-h/2)*Z+1);
    ctx.fillStyle='#fff'; ctx.fillText(t, o.x*Z, (o.y-h/2)*Z); ctx.textAlign='left' };
  for(const o of items){
    if(charZ!=null && !charDrawn && (o.z==null?o.y:o.z) > charZ) paintChar();
    if(o.hidden) continue;                       // kept in every way, simply not drawn""",
    "objects wear their name and code")

rep("""    if(spot){ const cx=z.pts.reduce((t,p)=>t+p[0],0)/z.pts.length,
                    cy=z.pts.reduce((t,p)=>t+p[1],0)/z.pts.length;
      ctx.fillStyle='rgba(145,81,211,1)'; ctx.font='700 '+(4*Z)+'px ui-sans-serif,system-ui';
      ctx.textAlign='center'; ctx.fillText('area '+z.spotId, cx*Z, cy*Z); ctx.textAlign='left' }""",
    """    if(!simOn && showGhosts){
      const cx=z.pts.reduce((t,p)=>t+p[0],0)/z.pts.length,
            cy=z.pts.reduce((t,p)=>t+p[1],0)/z.pts.length;
      ctx.fillStyle=walk?'rgba(60,180,90,1)':spot?'rgba(145,81,211,1)':'rgba(240,130,30,1)';
      ctx.font='700 '+(3.6*Z)+'px ui-sans-serif,system-ui'; ctx.textAlign='center';
      ctx.fillText(spot?('area '+z.spotId):walk?'walkable':'keep out', cx*Z, cy*Z);
      ctx.textAlign='left';
      if(z.on===false){ ctx.fillStyle='rgba(0,0,0,0.5)';
        ctx.fillText('(off)', cx*Z, (cy+5)*Z) }
    }""",
    "every zone wears its name, centred")

# ---------------------------------------------------------------- zones that switch off
rep("""function floorPoly(){ const z=scene.zones.find(q=>q.kind==='floor'&&q.pts&&q.pts.length>2);
  return z?z.pts:null }""",
    """function floorPoly(){ const z=scene.zones.find(q=>q.kind==='floor'&&q.on!==false&&q.pts&&q.pts.length>2);
  return z?z.pts:null }""",
    "an off floor falls back")

rep("""function zoneAt(x,y){
  for(let i=0;i<scene.zones.length;i++){ const z=scene.zones[i];
    if(z.kind==='keepout'&&z.pts&&z.pts.length>2&&inPoly(x,y,z.pts)) return i }
  return -1;
}""",
    """function zoneAt(x,y){
  for(let i=0;i<scene.zones.length;i++){ const z=scene.zones[i];
    if(z.kind==='keepout'&&z.on!==false&&z.pts&&z.pts.length>2&&inPoly(x,y,z.pts)) return i }
  return -1;
}""",
    "an off keep-out stops keeping out")

rep("""const areaOf=id=>scene.zones.find(z=>z.kind==='spot'&&z.spotId===id);""",
    """const areaOf=id=>scene.zones.find(z=>z.kind==='spot'&&z.on!==false&&z.spotId===id);""",
    "an off area is not offered")

rep("""  { const spots=scene.zones.filter(z=>z.kind==='spot');""",
    """  { const spots=scene.zones.filter(z=>z.kind==='spot'&&z.on!==false);""",
    "nor listed")

rep("""    walkable: scene.zones.filter(z=>z.kind==='floor').map(z=>({pts:z.pts})),""",
    """    walkable: scene.zones.filter(z=>z.kind==='floor'&&z.on!==false).map(z=>({pts:z.pts})),""",
    "nor exported (floor)")

rep("""    keepOut:  scene.zones.filter(z=>z.kind==='keepout').map(z=>({pts:z.pts})),""",
    """    keepOut:  scene.zones.filter(z=>z.kind==='keepout'&&z.on!==false).map(z=>({pts:z.pts})),""",
    "nor exported (keep-out)")

# a locked zone cannot be caught by its edge or nudged
rep("""  for(let i=scene.zones.length-1;i>=0;i--){
    const z=scene.zones[i];
    if(z.pts&&z.pts.length>2&&nearEdge(z.pts)){ zsel=i; zvert=-1; osel=-1; syncObj(); draw(); return } }""",
    """  for(let i=scene.zones.length-1;i>=0;i--){
    const z=scene.zones[i]; if(z.locked) continue;
    if(z.pts&&z.pts.length>2&&nearEdge(z.pts)){ zsel=i; zvert=-1; osel=-1; syncObj(); draw(); return } }""",
    "a locked zone ignores clicks")

rep("""  if(nud && osel<0 && zsel>=0 && scene.zones[zsel]){ const k=e.shiftKey?5:1;""",
    """  if(nud && osel<0 && zsel>=0 && scene.zones[zsel] && !scene.zones[zsel].locked){ const k=e.shiftKey?5:1;""",
    "nor arrows")

# ---------------------------------------------------------------- one list style for all three
rep("""  { const zs=scene.zones;
    $('zList').innerHTML = zs.length
      ? zs.map((z,i)=>`<button class="mini" data-z="${i}" style="${i===zsel?
          'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
          >${z.kind==='floor'?'walkable':z.kind==='spot'?('area '+z.spotId):'keep out'} ${(z.pts||[]).length}</button>`).join(' ')
      : 'none yet \\u2014 without a walkable shape the device keeps its own band';
    $('zList').querySelectorAll('[data-z]').forEach(b=>b.onclick=()=>{
      zsel=+b.dataset.z; osel=-1; syncObj(); draw() }); }""",
    """  { const zs=scene.zones;
    $('zList').innerHTML = zs.length
      ? zs.map((z,i)=>{ const nm=z.kind==='floor'?'walkable':z.kind==='spot'?('area '+z.spotId):'keep out';
        return `<div class="anim ${i===zsel?'sel':''} ${z.on===false?'off':''}">
          <button class="mini" data-zon="${i}" style="${z.on!==false?
            'background:var(--accent);color:#fff;border-color:var(--accent)':'opacity:.55'}"
            >${z.on!==false?'on':'off'}</button>
          <button class="mini" data-zlock="${i}" style="padding:0 4px"
            title="${z.locked?'locked - click to unlock':'lock it so clicks pass over it'}"
            >${z.locked?'\\u{1F512}':'\\u{1F513}'}</button>
          <span class="nm" data-z="${i}">${nm}</span>
          <span class="hint">${(z.pts||[]).length} pts</span>
          <button class="mini" data-zdel="${i}">&times;</button></div>`}).join('')
      : '<div class="hint">none yet \\u2014 without a walkable shape the device keeps its own band</div>';
    $('zList').querySelectorAll('[data-zon]').forEach(b=>b.onclick=()=>{
      const z=scene.zones[+b.dataset.zon]; z.on=(z.on===false); syncRule(); draw() });
    $('zList').querySelectorAll('[data-zlock]').forEach(b=>b.onclick=()=>{
      const z=scene.zones[+b.dataset.zlock]; z.locked=!z.locked;
      if(z.locked && zsel===+b.dataset.zlock) zsel=-1;
      draw() });
    $('zList').querySelectorAll('[data-zdel]').forEach(b=>b.onclick=()=>{
      scene.zones.splice(+b.dataset.zdel,1); zsel=-1; syncRule(); draw() });
    $('zList').querySelectorAll('[data-z]').forEach(b=>b.onclick=()=>{
      zsel=+b.dataset.z; osel=-1; syncObj(); draw() }); }""",
    "zones list like the animations")

# objects list: same row shape, with the code
rep("""    ? scene.objects.map((o,i)=>`<div class="obj" data-o="${i}" style="cursor:pointer;${
        i===osel?'color:var(--accent);font-weight:600':''}${o.locked?';opacity:.62':''}\"""",
    """    ? scene.objects.map((o,i)=>`<div class="anim ${i===osel?'sel':''} ${o.hidden?'off':''}"
        data-o="${i}" style="cursor:pointer;${o.locked?'opacity:.62':''}\"""",
    "object rows match")

# and the code chip on animation + object rows
rep("""      <span class="nm" data-pick="${i}">${a.name}</span>""",
    """      <span class="hint" style="font-weight:700">A${a.acode||'?'}</span>
      <span class="nm" data-pick="${i}">${a.name}</span>""",
    "the code sits in the animation row")

rep("""        >${o.locked?'\\u{1F512}':'\\u{1F513}'}</button>${nice(o.object)}""",
    """        >${o.locked?'\\u{1F512}':'\\u{1F513}'}</button
        ><span class="hint" style="font-weight:700;margin-right:4px">O${o.oid||'?'}</span>${nice(o.object)}""",
    "and in the object row")

rep("""  $('anims').innerHTML = anims.length ? anims.map((a,i)=>{""",
    """  ensureAcodes();
  $('anims').innerHTML = anims.length ? anims.map((a,i)=>{""",
    "codes exist before the list draws")

# the object labels have to be painted after each sprite
rep("""    ctx.restore();
    ctx.save(); ctx.globalAlpha=(o.opacity==null?100:o.opacity)/100;""",
    """    ctx.restore();
    ctx.save(); ctx.globalAlpha=(o.opacity==null?100:o.opacity)/100;""",
    "(anchor check)")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
