"""An ACTIVITY AREA: a drawn patch of floor an animation may be bound to.

Jon: "lets say for example on the left side of the screen i want him to be able to sit facing
east but only on a defined area" - a place-rule with no object in it. So the WHERE of an
animation now has three answers, not two:

    anywhere he walks         wherever he can stand
    where something affords   at an object marked with what it needs
    in a drawn area           inside an activity area you drew

Each area gets a number when it is drawn ("area 1"), the rule panel lists them, and the sim
treats one like an object without art: walk to a standable point inside it, settle there, play
facing whichever way the facing picker says, step out, wander. Purple, next to the green floor
and orange keep-outs.
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


# ---------------------------------------------------------------- drawing one
rep("""        <button class="mini" id="zKeep">+ keep out</button>""",
    """        <button class="mini" id="zKeep">+ keep out</button>
        <button class="mini" id="zSpot" title="a patch of floor an animation can be tied to">+ activity area</button>""",
    "the button")

rep("""$('zFloor').onclick=()=>{ zdraft=null; zmode=zmode==='floor'?'':'floor';
  $('zFloor').classList.toggle('pri',zmode==='floor'); $('zKeep').classList.remove('pri'); draw() };""",
    """$('zFloor').onclick=()=>{ zdraft=null; zmode=zmode==='floor'?'':'floor';
  $('zFloor').classList.toggle('pri',zmode==='floor'); $('zKeep').classList.remove('pri');
  $('zSpot').classList.remove('pri'); draw() };
$('zSpot').onclick=()=>{ zdraft=null; zmode=zmode==='spot'?'':'spot';
  $('zSpot').classList.toggle('pri',zmode==='spot');
  $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri'); draw() };""",
    "wire it")

rep("""function closeZone(){
  if(zdraft && zdraft.pts.length>=3){ scene.zones.push({kind:zdraft.kind,pts:zdraft.pts});""",
    """let spotSeq=1;
function closeZone(){
  if(zdraft && zdraft.pts.length>=3){
    const z={kind:zdraft.kind, pts:zdraft.pts};
    // an area is a thing an animation refers to, so it needs a name that survives reordering
    if(z.kind==='spot'){ while(scene.zones.some(q=>q.spotId===spotSeq)) spotSeq++;
      z.spotId=spotSeq++; }
    scene.zones.push(z);""",
    "areas get a lasting number")

rep("""  $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri'); draw();
}""",
    """  $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri');
  $('zSpot').classList.remove('pri'); draw();
}""",
    "and the mode clears")

# ---------------------------------------------------------------- drawing them purple
rep("""    const walk=z.kind==='floor';
    ctx.fillStyle=walk?'rgba(60,180,90,0.15)':'rgba(240,130,30,0.20)';
    ctx.fill();
    ctx.strokeStyle=walk?'rgba(60,180,90,0.9)':'rgba(240,130,30,0.9)';
    ctx.lineWidth=2; ctx.setLineDash([6,4]); ctx.stroke(); ctx.setLineDash([]);""",
    """    const walk=z.kind==='floor', spot=z.kind==='spot';
    ctx.fillStyle=walk?'rgba(60,180,90,0.15)':spot?'rgba(145,81,211,0.16)':'rgba(240,130,30,0.20)';
    ctx.fill();
    ctx.strokeStyle=walk?'rgba(60,180,90,0.9)':spot?'rgba(145,81,211,0.9)':'rgba(240,130,30,0.9)';
    ctx.lineWidth=2; ctx.setLineDash([6,4]); ctx.stroke(); ctx.setLineDash([]);
    if(spot){ const cx=z.pts.reduce((t,p)=>t+p[0],0)/z.pts.length,
                    cy=z.pts.reduce((t,p)=>t+p[1],0)/z.pts.length;
      ctx.fillStyle='rgba(145,81,211,1)'; ctx.font='700 '+(4*Z)+'px ui-sans-serif,system-ui';
      ctx.textAlign='center'; ctx.fillText('area '+z.spotId, cx*Z, cy*Z); ctx.textAlign='left' }""",
    "purple, and labelled")

rep("""          >${z.kind==='floor'?'walkable':'keep out'} ${(z.pts||[]).length}</button>`).join(' ')""",
    """          >${z.kind==='floor'?'walkable':z.kind==='spot'?('area '+z.spotId):'keep out'} ${(z.pts||[]).length}</button>`).join(' ')""",
    "named in the list")

# ---------------------------------------------------------------- the rule
rep("""  $('rNeeds').innerHTML=""",
    """  { const spots=scene.zones.filter(z=>z.kind==='spot');
    $('rArea').innerHTML = spots.length
      ? '<option value="">in a drawn area\\u2026</option>'+spots.map(z=>
          `<option value="${z.spotId}"${a.rule&&a.rule.inArea===z.spotId?' selected':''}>only in area ${z.spotId}</option>`).join('')
      : '<option value="">no areas drawn yet</option>';
    $('rArea').disabled=!spots.length; }
  $('rNeeds').innerHTML=""",
    "the rule offers the areas")

rep("""          <select id="rNeeds" style="flex:1"></select>""",
    """          <select id="rNeeds" style="flex:1"></select>
          <select id="rArea" style="flex:1"></select>""",
    "its select")

rep("""$('rNeeds').onchange=e=>{ const a=anims[asel]; if(!a)return;""",
    """$('rArea').onchange=e=>{ const a=anims[asel]; if(!a)return;
  if(e.target.value) a.rule={inArea:+e.target.value};
  syncRule(); draw() };
$('rNeeds').onchange=e=>{ const a=anims[asel]; if(!a)return;""",
    "wire the rule")

# ---------------------------------------------------------------- the sim understands it
rep("""function placesFor(a){
  if(!ruleNeeds(a)) return {kind:'anywhere', spots:[]};""",
    """const areaOf=id=>scene.zones.find(z=>z.kind==='spot'&&z.spotId===id);
// somewhere standable inside an area - its centroid if that works, else a scan
function areaPoint(z){
  const cx=z.pts.reduce((t,p)=>t+p[0],0)/z.pts.length,
        cy=z.pts.reduce((t,p)=>t+p[1],0)/z.pts.length;
  if(standable(cx,cy)&&inPoly(cx,cy,z.pts)) return {x:cx,y:cy};
  const xs=z.pts.map(p=>p[0]), ys=z.pts.map(p=>p[1]);
  for(let t=0;t<120;t++){
    const x=Math.min(...xs)+Math.random()*(Math.max(...xs)-Math.min(...xs));
    const y=Math.min(...ys)+Math.random()*(Math.max(...ys)-Math.min(...ys));
    if(inPoly(x,y,z.pts)&&standable(x,y)) return {x,y} }
  return null;
}
function placesFor(a){
  if(a.rule&&a.rule.inArea!=null){
    const z=areaOf(a.rule.inArea);
    const p=z&&areaPoint(z);
    // shaped like an object so everything downstream works unchanged
    return {kind:'needs', spots:p?[{x:p.x,y:p.y,object:'area '+a.rule.inArea,area:z}]:[]};
  }
  if(!ruleNeeds(a)) return {kind:'anywhere', spots:[]};""",
    "an area is a place")

rep("""  $('rAny').classList.toggle('pri', !ruleNeeds(a));""",
    """  $('rAny').classList.toggle('pri', !ruleNeeds(a) && !(a.rule&&a.rule.inArea!=null));""",
    "the buttons reflect it")

# nice() would mangle "area 1"; guard it in the sim's speech
rep("""       says:c.spot?`walking to the ${nice(c.spot.object)}`:'wandering'};""",
    """       says:c.spot?`walking to the ${c.spot.area?c.spot.object:nice(c.spot.object)}`:'wandering'};""",
    "he says it plainly")

# the export carries it
rep("""      facing: isRot(a.clip) ? (a.facing||faceOnOf(a.clip)) : null,""",
    """      facing: isRot(a.clip) ? (a.facing||faceOnOf(a.clip)) : null,
      inArea: (a.rule&&a.rule.inArea!=null)?a.rule.inArea:null,""",
    "exported")

rep("""  scene.zones=(s.zones||[]).map(z=>({kind:z.kind,pts:z.pts}))""",
    """  scene.zones=(s.zones||[]).map(z=>({kind:z.kind,pts:z.pts,spotId:z.spotId}))""",
    "and re-imported")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
