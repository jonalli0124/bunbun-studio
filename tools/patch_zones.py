"""Keep-out zones, a walkable floor, and what each object AFFORDS.

Built to the scene contract (project-bunbun-scene-contract):

    the scene declares only geometry and intent - WHERE they can walk, where they cannot,
    and which places may be used for what. The sim decides what actually happens from a seed.

So an object does not say "the cat sleeps here at 3pm". It says "this may be slept on", and
the sim picks. Rectangles rather than polygons on purpose: scene_push turns a floor polygon and
keep-outs into one walkable rectangle plus Blocks anyway, which is the model the pet walks.
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


rep("let rprops=[], rsel=-1;",
    """let rprops=[], rsel=-1;
// The scene says WHERE, never what happens. A floor is where they may walk, a keep-out is
// where they may not, and an affordance is a permission - "this may be sat on" - not a script.
let zones=[], zsel=-1, zdraw=null, zmode='';
const AFFORD=[['sit','may be sat on'],['sleep','may be slept on'],
              ['surface','things may be put on it'],['eat','may be eaten at'],
              ['play','may be played with']];""",
    "zone state")

# ---- drawing the zones, editor only ----
rep("  if(showSel) S_ZONES_HERE();", "  if(showSel) S_ZONES_HERE();", "noop")
rep("""  if(showSel){                                              // floor guide, editor only""",
    """  if(showSel){                                              // zones, editor only
    for(let i=0;i<zones.length;i++){const z=zones[i];
      const x=Math.min(z.x0,z.x1)*Zg, y=Math.min(z.y0,z.y1)*Zg;
      const w=Math.abs(z.x1-z.x0)*Zg, h=Math.abs(z.y1-z.y0)*Zg;
      const floor=z.kind==='floor';
      g.fillStyle=floor?'rgba(60,180,90,0.16)':'rgba(240,130,30,0.20)';
      g.fillRect(x,y,w,h);
      g.strokeStyle=floor?'rgba(60,180,90,0.95)':'rgba(240,130,30,0.95)';
      g.lineWidth=(i===zsel)?3:2; g.setLineDash(i===zsel?[]:[6,4]);
      g.strokeRect(x,y,w,h); g.setLineDash([]);
      g.fillStyle=g.strokeStyle; g.font='700 '+(4*Zg)+'px ui-sans-serif,system-ui';
      g.fillText(floor?'walkable':'keep out', x+3, y+5*Zg);
    }
    if(zdraw){
      const x=Math.min(zdraw.x0,zdraw.x1)*Zg, y=Math.min(zdraw.y0,zdraw.y1)*Zg;
      g.strokeStyle='#fff'; g.lineWidth=2; g.setLineDash([4,4]);
      g.strokeRect(x,y,Math.abs(zdraw.x1-zdraw.x0)*Zg,Math.abs(zdraw.y1-zdraw.y0)*Zg);
      g.setLineDash([]);
    }
                                                              // floor guide, editor only""",
    "draw zones")

# ---- markup ----
rep("""      <div class="row" style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <select id="rpAdd"></select>""",
    """      <div style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <label>zones <span class="hint">&mdash; drag on the room to draw one</span></label>
        <div class="row">
          <button class="mini" id="zFloor">+ walkable floor</button>
          <button class="mini" id="zKeep">+ keep out</button>
          <button class="mini" id="zDel">remove</button>
        </div>
        <div class="hint" id="zList" style="margin-top:6px"></div>
      </div>

      <div class="row" style="margin-top:10px;padding-top:10px;border-top:1px solid var(--line)">
        <select id="rpAdd"></select>""",
    "zone markup")

rep("""        <div class="hint" id="rpWhere"></div>""",
    """        <div style="margin-top:8px">
          <label>what can be done here <span class="hint">&mdash; a permission, not a script</span></label>
          <div id="rpCan" class="row" style="flex-wrap:wrap;gap:5px"></div>
        </div>
        <div class="hint" id="rpWhere"></div>""",
    "affordance markup")

# ---- interaction: drag out a zone when a mode is armed ----
rep("""  gv.addEventListener('pointerdown',e=>{ const q=at(e); const hit=pick(q);""",
    """  gv.addEventListener('pointerdown',e=>{ const q=at(e);
    if(zmode){ zdraw={kind:zmode,x0:q.x,y0:q.y,x1:q.x,y1:q.y};
      gv.setPointerCapture(e.pointerId); render(); return }
    const hit=pick(q);""",
    "start a zone drag")
rep("""  gv.addEventListener('pointermove',e=>{ if(!dg)return; const q=at(e);""",
    """  gv.addEventListener('pointermove',e=>{
    if(zdraw){ const q=at(e); zdraw.x1=q.x; zdraw.y1=q.y; render(); return }
    if(!dg)return; const q=at(e);""",
    "size the zone")
rep("""  gv.addEventListener('pointerup',()=>{ dg=null; gv.style.cursor='grab' });""",
    """  gv.addEventListener('pointerup',()=>{
    if(zdraw){
      if(Math.abs(zdraw.x1-zdraw.x0)>6&&Math.abs(zdraw.y1-zdraw.y0)>4){
        zones.push({kind:zdraw.kind,
          x0:Math.min(zdraw.x0,zdraw.x1), y0:Math.min(zdraw.y0,zdraw.y1),
          x1:Math.max(zdraw.x0,zdraw.x1), y1:Math.max(zdraw.y0,zdraw.y1)});
        zsel=zones.length-1;
      }
      zdraw=null; zmode=''; $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri');
      render(); return }
    dg=null; gv.style.cursor='grab' });""",
    "finish the zone")

# ---- controls ----
rep("$('gscale').oninput=e=>{ gScale=+e.target.value; render() };",
    """$('zFloor').onclick=()=>{ zmode=zmode==='floor'?'':'floor';
  $('zFloor').classList.toggle('pri',zmode==='floor'); $('zKeep').classList.remove('pri'); render() };
$('zKeep').onclick=()=>{ zmode=zmode==='keepout'?'':'keepout';
  $('zKeep').classList.toggle('pri',zmode==='keepout'); $('zFloor').classList.remove('pri'); render() };
$('zDel').onclick=()=>{ if(zsel>=0){ zones.splice(zsel,1); zsel=-1; render() } };
$('gscale').oninput=e=>{ gScale=+e.target.value; render() };""",
    "zone controls")

rep("  rpDraw();",
    """  rpDraw();
  { const f=zones.filter(z=>z.kind==='floor').length, k=zones.length-f;
    $('zList').innerHTML = zones.length
      ? zones.map((z,i)=>`<button class="mini" data-z="${i}" style="${i===zsel?
          'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
          >${z.kind==='floor'?'floor':'keep out'} ${Math.round(z.x1-z.x0)}\\u00d7${Math.round(z.y1-z.y0)}</button>`).join(' ')
        + `<span class="hint"> &mdash; ${f} walkable, ${k} keep-out</span>`
      : 'none yet \\u2014 with none, the whole floor is walkable';
    $('zList').querySelectorAll('[data-z]').forEach(b=>b.onclick=()=>{ zsel=+b.dataset.z; render() }); }""",
    "zone list")

rep("""    $('rpWhere').textContent=`at ${Math.round(p.x)},${Math.round(p.y)} `+""",
    """    { const can=p.can||[];
      $('rpCan').innerHTML=AFFORD.map(([k,label])=>
        `<button class="mini" data-can="${k}" title="${label}" style="${can.includes(k)?
          'background:var(--accent);color:#fff;border-color:var(--accent)':''}">${k}</button>`).join('');
      $('rpCan').querySelectorAll('[data-can]').forEach(b=>b.onclick=()=>{
        const k=b.dataset.can, set=new Set(p.can||[]);
        set.has(k)?set.delete(k):set.add(k);
        p.can=[...set]; render() }); }
    $('rpWhere').textContent=`at ${Math.round(p.x)},${Math.round(p.y)} `+""",
    "affordance buttons")

# ---- export ----
rep("""           weather:{rain:raining, clouds:clouding, style:cloudStyle,""",
    """           floor:zones.filter(z=>z.kind==='floor')
                   .map(z=>({x0:Math.round(z.x0),y0:Math.round(z.y0),
                             x1:Math.round(z.x1),y1:Math.round(z.y1)})),
           keepOut:zones.filter(z=>z.kind==='keepout')
                   .map(z=>({x0:Math.round(z.x0),y0:Math.round(z.y0),
                             x1:Math.round(z.x1),y1:Math.round(z.y1)})),
           weather:{rain:raining, clouds:clouding, style:cloudStyle,""",
    "zones in the export")

rep("""             opacity:p.opac==null?100:p.opac, z:(p.z==null?p.y:p.z), zAuto:p.z==null,""",
    """             opacity:p.opac==null?100:p.opac, z:(p.z==null?p.y:p.z), zAuto:p.z==null,
             can:(p.can&&p.can.length)?p.can:null,
             usePoint:(p.can&&p.can.length)?{x:Math.round(p.x),y:Math.round(p.y)}:null,""",
    "affordances in the export")

# ---- persistence ----
rep("   light:{todMin,todOn,lampMode,lightScale},",
    "   zones:zones.map(z=>({...z})),\n   light:{todMin,todOn,lampMode,lightScale},",
    "save zones")
rep("    rsel=-1;",
    """    rsel=-1;
    zones=Array.isArray(p.zones)?p.zones.filter(z=>z&&(z.kind==='floor'||z.kind==='keepout'))
      .map(z=>({kind:z.kind,x0:+z.x0,y0:+z.y0,x1:+z.x1,y1:+z.y1})):[];
    zsel=-1; zdraw=null; zmode='';""",
    "load zones")

# ---- and the duplicate-source bug Jon's export exposed ----
rep("""  for(const o of live){ const fr=DATA.clips[clip].frames[o.i];
    files.push({name:`${root}/assets/source/${fr.name}`,data:b64Bytes(fr.img)}) }""",
    """  // once per SOURCE frame, not once per played frame - a held clip repeats the same file
  for(const i of [...new Set(live.map(o=>o.i))]){ const fr=DATA.clips[clip].frames[i];
    files.push({name:`${root}/assets/source/${fr.name}`,data:b64Bytes(fr.img)}) }""",
    "dedupe the source frames")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits applied")
