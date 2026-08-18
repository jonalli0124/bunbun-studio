"""Action placeholders, and a library of saved animations.

ACTION MARKS are the builder's own idea: a placed pose is a STAGE MARK, not scenery. Put `sit`
on the chair and the sim walks there and sits IN THAT POSE. The mark is an instruction, so it is
ghosted while editing and never drawn in an export.

THE LIBRARY is every composition you have saved, by name, so two different play animations can
live side by side and be cycled through to edit. Saved in the browser, exportable as one file.
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


# ---------- 1. the zone pick order ----------
rep("""    { const hit=zones.findIndex(z=>z.pts&&z.pts.length>2&&inPoly(q.x,q.y,z.pts));""",
    """    { // search from the top: a keep-out drawn inside the floor must win over the floor
      let hit=-1;
      for(let i=zones.length-1;i>=0;i--){
        const z=zones[i];
        if(z.pts&&z.pts.length>2&&inPoly(q.x,q.y,z.pts)){ hit=i; break } }""",
    "topmost zone wins")

# ---------- 2. action marks ----------
rep("let zones=[], zsel=-1, zdraft=null, zmode='', zvert=-1;",
    """let zones=[], zsel=-1, zdraft=null, zmode='', zvert=-1;
// A placed pose is an INSTRUCTION: "this action may happen here, in this pose". It is ghosted
// while you work and never drawn in an export, because during a run the live actor performs it.
let acts=[], asel=-1, amode='';""",
    "action-mark state")

rep("""      <div style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <label>zones <span class="hint">&mdash; drag on the room to draw one</span></label>""",
    """      <div style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <label>where actions can happen
          <span class="hint">&mdash; a pose placed in the room is an instruction, not scenery</span></label>
        <div class="row">
          <select id="aClip"></select>
          <button style="flex:0 0 auto" class="mini" id="aAdd">+ place it</button>
          <button style="flex:0 0 auto" class="mini" id="aDel">remove</button>
        </div>
        <div class="hint" id="aList" style="margin-top:6px"></div>
      </div>

      <div style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <label>zones <span class="hint">&mdash; drag on the room to draw one</span></label>""",
    "action-mark markup")

rep("""    if(zmode){
      if(!zdraft) zdraft={kind:zmode,pts:[]};""",
    """    if(amode){ acts.push({clip:amode,x:q.x,y:q.y}); asel=acts.length-1; amode='';
      $('aAdd').classList.remove('pri'); render(); return }
    { const hitA=acts.findIndex(a=>Math.abs(a.x-q.x)<14&&Math.abs(a.y-q.y)<20);
      if(hitA>=0 && !zmode){ asel=hitA; dg={kind:'act',dx:acts[hitA].x-q.x,dy:acts[hitA].y-q.y};
        gv.setPointerCapture(e.pointerId); render(); return } }
    if(zmode){
      if(!zdraft) zdraft={kind:zmode,pts:[]};""",
    "place and grab a mark")

rep("""    if(dg.kind==='prop'){ const p=rprops[rsel]; if(p){ p.x=q.x+dg.dx; p.y=q.y+dg.dy } }""",
    """    if(dg.kind==='act'){ const a=acts[asel]; if(a){ a.x=q.x+dg.dx; a.y=q.y+dg.dy } }
    else if(dg.kind==='prop'){ const p=rprops[rsel]; if(p){ p.x=q.x+dg.dx; p.y=q.y+dg.dy } }""",
    "drag a mark")

# ghosted, and only while editing
rep("""  if(showSel){                                              // zones, editor only""",
    """  if(showSel) for(let i=0;i<acts.length;i++){              // stage marks, editor only
    const a=acts[i], c=DATA.clips[a.clip];
    if(!c) continue;
    const im=imgs[a.clip+'|0'];
    const sc=(gScale==null?defaultScale():gScale)/100;
    if(im&&im.complete){
      g.save(); g.globalAlpha=i===asel?0.55:0.34;
      g.drawImage(im,(a.x-S/2*sc)*Zg,(a.y-90*sc)*Zg,S*sc*Zg,S*sc*Zg);
      g.restore(); }
    g.strokeStyle=i===asel?'#e8392d':'rgba(232,57,45,0.6)';
    g.lineWidth=2; g.setLineDash([4,3]);
    g.beginPath(); g.ellipse(a.x*Zg,a.y*Zg,9*Zg,3*Zg,0,0,7); g.stroke(); g.setLineDash([]);
    g.fillStyle=g.strokeStyle; g.font='700 '+(4*Zg)+'px ui-sans-serif,system-ui';
    g.textAlign='center'; g.fillText(a.clip.replace(/^(Adult|Baby)_/,''),a.x*Zg,(a.y-8)*Zg);
    g.textAlign='left';
  }
  if(showSel){                                              // zones, editor only""",
    "draw the ghosted marks")

rep("""$('zClose').onclick=closeZone;""",
    """$('zClose').onclick=closeZone;
$('aAdd').onclick=()=>{ amode=amode?'':$('aClip').value; zmode=''; zdraft=null;
  $('aAdd').classList.toggle('pri',!!amode);
  $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri'); render() };
$('aDel').onclick=()=>{ if(asel>=0){ acts.splice(asel,1); asel=-1; render() } };""",
    "action-mark controls")

rep("""  { const f=zones.filter(z=>z.kind==='floor').length, k=zones.length-f;""",
    """  $('aList').innerHTML = acts.length
    ? acts.map((a,i)=>`<button class="mini" data-a2="${i}" style="${i===asel?
        'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
        >${a.clip.replace(/^(Adult|Baby)_/,'')} @${Math.round(a.x)},${Math.round(a.y)}</button>`).join(' ')
    : 'none \\u2014 place a pose where you want it to be able to happen';
  $('aList').querySelectorAll('[data-a2]').forEach(b=>b.onclick=()=>{ asel=+b.dataset.a2; render() });
  { const f=zones.filter(z=>z.kind==='floor').length, k=zones.length-f;""",
    "action-mark list")

rep("""  const rk=Object.keys(DATA.rooms||{});""",
    """  $('aClip').innerHTML=Object.keys(DATA.clips).map(k=>`<option>${k}</option>`).join('');
  const rk=Object.keys(DATA.rooms||{});""",
    "populate the pose picker")

# export + persistence
rep("""           walkable:zones.filter(z=>z.kind==='floor')""",
    """           actions:acts.map(a=>({pose:a.clip,x:Math.round(a.x),y:Math.round(a.y)})),
           walkable:zones.filter(z=>z.kind==='floor')""",
    "marks in the export")
rep("   zones:zones.map(z=>({...z})),",
    "   zones:zones.map(z=>({...z})), actions:acts.map(a=>({...a})),",
    "save marks")
rep("    zsel=-1; zdraft=null; zmode=''; zvert=-1;",
    """    zsel=-1; zdraft=null; zmode=''; zvert=-1;
    acts=Array.isArray(p.actions)?p.actions.filter(a=>a&&DATA.clips[a.clip||a.pose])
      .map(a=>({clip:a.clip||a.pose,x:+a.x,y:+a.y})):[];
    asel=-1; amode='';""",
    "load marks")

# ---------- 3. the library ----------
rep("""      <label>project <span class="hint">&mdash; what you set. Re-openable.</span></label>""",
    """      <label>library <span class="hint">&mdash; every animation you have saved</span></label>
      <div class="row">
        <select id="libSel"></select>
        <button style="flex:0 0 auto" class="mini" id="libPrev">&#9664;</button>
        <button style="flex:0 0 auto" class="mini" id="libNext">&#9654;</button>
      </div>
      <div class="row" style="margin-top:6px">
        <input id="libName" placeholder="name this one" style="flex:1;padding:6px;
          border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink);font:inherit">
        <button style="flex:0 0 auto" class="mini pri" id="libSave">keep</button>
        <button style="flex:0 0 auto" class="mini" id="libDel">forget</button>
      </div>
      <div class="hint" id="libInfo" style="margin-top:6px"></div>

      <label style="margin-top:12px">project <span class="hint">&mdash; what you set. Re-openable.</span></label>""",
    "library markup")

rep("""// autosave the working state so a reload does not lose placement work""",
    """// ---- the library: named animations, kept in the browser ----
const LIB='bunbun-attach-library';
const libAll=()=>{ try{ return JSON.parse(localStorage.getItem(LIB)||'{}') }catch(e){ return {} } };
function libDraw(){
  const all=libAll(), names=Object.keys(all).sort();
  const cur=$('libSel').value;
  $('libSel').innerHTML=names.length
    ? names.map(n=>`<option${n===cur?' selected':''}>${n}</option>`).join('')
    : '<option value="">(nothing kept yet)</option>';
  $('libInfo').textContent=names.length
    ? `${names.length} kept \\u2014 pick one and it loads, or use \\u25c0 \\u25b6 to cycle`
    : 'name the current animation and press keep';
}
function libLoadName(n){
  const all=libAll(); if(!all[n]) return;
  loadProject(all[n]).then(()=>{ $('libName').value=n; flash('opened "'+n+'"') })
    .catch(e=>flash('could not open "'+n+'" \\u2014 '+e.message,true));
}
$('libSave').onclick=()=>{
  const n=($('libName').value||'').trim().slice(0,40);
  if(!n) return flash('give it a name first',true);
  const all=libAll(); all[n]=projectCore();
  try{ localStorage.setItem(LIB,JSON.stringify(all)) }
  catch(e){ return flash('no room left in the browser to keep another',true) }
  libDraw(); $('libSel').value=n; flash('kept "'+n+'"');
};
$('libDel').onclick=()=>{ const n=$('libSel').value; if(!n) return;
  const all=libAll(); delete all[n];
  try{ localStorage.setItem(LIB,JSON.stringify(all)) }catch(e){}
  libDraw(); flash('forgot "'+n+'"') };
$('libSel').onchange=e=>libLoadName(e.target.value);
const libStep=d=>{ const names=Object.keys(libAll()).sort(); if(!names.length) return;
  const i=names.indexOf($('libSel').value);
  const n=names[((i<0?0:i+d)+names.length)%names.length];
  $('libSel').value=n; libLoadName(n) };
$('libPrev').onclick=()=>libStep(-1);
$('libNext').onclick=()=>libStep(1);

// autosave the working state so a reload does not lose placement work""",
    "library logic")

rep("  booting=false;",
    "  libDraw();\n  booting=false;",
    "draw the library at boot")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
