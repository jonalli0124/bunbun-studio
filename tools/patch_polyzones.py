"""Zones become POLYGONS, and existing ones can be selected and reshaped.

A room drawn in perspective has a trapezoid floor. A rectangle either fences off real floor
along the back wall or lets him stand off the front edge - which is how bunbun came to stand on
the skirting. scene_push already takes polygons (`polys[] {pts, floor}`) and derives the
device's walkable rectangle from them itself, so a polygon is also the shape the pipeline wants.
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


rep("""let zones=[], zsel=-1, zdraw=null, zmode='';""",
    """let zones=[], zsel=-1, zdraft=null, zmode='', zvert=-1;
const inPoly=(x,y,pts)=>{            // ray cast, so a concave floor still tests correctly
  let hit=false;
  for(let i=0,j=pts.length-1;i<pts.length;j=i++){
    const [xi,yi]=pts[i],[xj,yj]=pts[j];
    if(((yi>y)!==(yj>y)) && x < (xj-xi)*(y-yi)/((yj-yi)||1e-9)+xi) hit=!hit }
  return hit};
const nearPt=(x,y,p,r)=>Math.abs(x-p[0])<=r && Math.abs(y-p[1])<=r;""",
    "polygon state + hit tests")

# ---- drawing ----
rep("""    for(let i=0;i<zones.length;i++){const z=zones[i];
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
    }""",
    """    const tracePoly=(pts,close)=>{ g.beginPath();
      pts.forEach((p,i)=>i?g.lineTo(p[0]*Zg,p[1]*Zg):g.moveTo(p[0]*Zg,p[1]*Zg));
      if(close) g.closePath() };
    for(let i=0;i<zones.length;i++){const z=zones[i];
      if(!z.pts||z.pts.length<3) continue;
      const floor=z.kind==='floor';
      tracePoly(z.pts,true);
      g.fillStyle=floor?'rgba(60,180,90,0.16)':'rgba(240,130,30,0.20)'; g.fill();
      g.strokeStyle=floor?'rgba(60,180,90,0.95)':'rgba(240,130,30,0.95)';
      g.lineWidth=(i===zsel)?3:2; g.setLineDash(i===zsel?[]:[6,4]); g.stroke(); g.setLineDash([]);
      const cx=z.pts.reduce((a,p)=>a+p[0],0)/z.pts.length;
      const cy=z.pts.reduce((a,p)=>a+p[1],0)/z.pts.length;
      g.fillStyle=g.strokeStyle; g.font='700 '+(4*Zg)+'px ui-sans-serif,system-ui';
      g.textAlign='center'; g.fillText(floor?'walkable':'keep out',cx*Zg,cy*Zg); g.textAlign='left';
      if(i===zsel) z.pts.forEach((p,vi)=>{            // handles, so it can be reshaped
        g.fillStyle=vi===zvert?'#fff':g.strokeStyle;
        g.fillRect(p[0]*Zg-3,p[1]*Zg-3,6,6);
        g.strokeStyle='#000'; g.lineWidth=1; g.strokeRect(p[0]*Zg-3,p[1]*Zg-3,6,6);
        g.strokeStyle=floor?'rgba(60,180,90,0.95)':'rgba(240,130,30,0.95)'; });
    }
    if(zdraft&&zdraft.pts.length){
      tracePoly(zdraft.pts,false);
      g.strokeStyle='#fff'; g.lineWidth=2; g.setLineDash([4,4]); g.stroke(); g.setLineDash([]);
      zdraft.pts.forEach(p=>{ g.fillStyle='#fff'; g.fillRect(p[0]*Zg-3,p[1]*Zg-3,6,6) });
      const f=zdraft.pts[0];
      g.strokeStyle='#fff'; g.lineWidth=1;
      g.strokeRect(f[0]*Zg-5,f[1]*Zg-5,10,10);       // click here to close
    }""",
    "draw polygons")

# ---- interaction ----
rep("""    if(zmode){ zdraw={kind:zmode,x0:q.x,y0:q.y,x1:q.x,y1:q.y};
      gv.setPointerCapture(e.pointerId); render(); return }""",
    """    if(zmode){
      if(!zdraft) zdraft={kind:zmode,pts:[]};
      const first=zdraft.pts[0];
      if(first && zdraft.pts.length>=3 && nearPt(q.x,q.y,first,5)){ closeZone(); return }
      zdraft.pts.push([q.x,q.y]); render(); return }
    // grab a handle of the selected zone, or select the zone under the cursor
    if(zsel>=0 && zones[zsel] && zones[zsel].pts){
      const vi=zones[zsel].pts.findIndex(p=>nearPt(q.x,q.y,p,5));
      if(vi>=0){ zvert=vi; gv.setPointerCapture(e.pointerId); render(); return } }
    { const hit=zones.findIndex(z=>z.pts&&z.pts.length>2&&inPoly(q.x,q.y,z.pts));
      if(hit>=0 && !pickAt(q)){ zsel=hit; zvert=-1; render(); return } }""",
    "click to draw, select, or grab a handle")

rep("""  gv.addEventListener('pointermove',e=>{
    if(zdraw){ const q=at(e); zdraw.x1=q.x; zdraw.y1=q.y; render(); return }""",
    """  gv.addEventListener('pointermove',e=>{
    if(zvert>=0 && zsel>=0){ const q=at(e); zones[zsel].pts[zvert]=[q.x,q.y]; render(); return }
    if(zdraft){ render(); return }""",
    "drag a handle")

rep("""  gv.addEventListener('pointerup',()=>{
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
    """  gv.addEventListener('pointerup',()=>{
    if(zvert>=0){ zvert=-1; render(); return }
    dg=null; gv.style.cursor='grab' });""",
    "release a handle")

rep("""$('zFloor').onclick=()=>{ zmode=zmode==='floor'?'':'floor';""",
    """function closeZone(){
  if(zdraft && zdraft.pts.length>=3){ zones.push({kind:zdraft.kind,pts:zdraft.pts});
    zsel=zones.length-1 }
  zdraft=null; zmode=''; zvert=-1;
  $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri');
  render();
}
$('zClose').onclick=closeZone;
$('zFloor').onclick=()=>{ zdraft=null; zmode=zmode==='floor'?'':'floor';""",
    "closeZone")
rep("""$('zKeep').onclick=()=>{ zmode=zmode==='keepout'?'':'keepout';""",
    """$('zKeep').onclick=()=>{ zdraft=null; zmode=zmode==='keepout'?'':'keepout';""",
    "keepout resets the draft")
rep("""$('zDel').onclick=()=>{ if(zsel>=0){ zones.splice(zsel,1); zsel=-1; render() } };""",
    """$('zDel').onclick=()=>{ if(zsel>=0){ zones.splice(zsel,1); zsel=-1; zvert=-1; render() } };""",
    "delete clears the handle")

rep("""        <div class="row">
          <button class="mini" id="zFloor">+ walkable floor</button>
          <button class="mini" id="zKeep">+ keep out</button>
          <button class="mini" id="zDel">remove</button>
        </div>""",
    """        <div class="row">
          <button class="mini" id="zFloor">+ walkable floor</button>
          <button class="mini" id="zKeep">+ keep out</button>
          <button class="mini" id="zClose">finish shape</button>
          <button class="mini" id="zDel">remove</button>
        </div>
        <div class="hint">click round the shape, then click the first corner again (or
          <b>finish shape</b>). Click inside a finished one to select it, then drag its corners.</div>""",
    "polygon instructions")

rep("""      ? zones.map((z,i)=>`<button class="mini" data-z="${i}" style="${i===zsel?
          'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
          >${z.kind==='floor'?'floor':'keep out'} ${Math.round(z.x1-z.x0)}\\u00d7${Math.round(z.y1-z.y0)}</button>`).join(' ')""",
    """      ? zones.map((z,i)=>`<button class="mini" data-z="${i}" style="${i===zsel?
          'background:var(--accent);color:#fff;border-color:var(--accent)':''}"
          >${z.kind==='floor'?'floor':'keep out'} ${(z.pts||[]).length} corners</button>`).join(' ')""",
    "zone list shows corners")

# ---- export + persistence ----
rep("""           walkable:zones.filter(z=>z.kind==='floor')
                   .map(z=>({x0:Math.round(z.x0),y0:Math.round(z.y0),
                             x1:Math.round(z.x1),y1:Math.round(z.y1)})),
           keepOut:zones.filter(z=>z.kind==='keepout')
                   .map(z=>({x0:Math.round(z.x0),y0:Math.round(z.y0),
                             x1:Math.round(z.x1),y1:Math.round(z.y1)})),""",
    """           walkable:zones.filter(z=>z.kind==='floor')
                   .map(z=>({pts:z.pts.map(p=>[Math.round(p[0]),Math.round(p[1])])})),
           keepOut:zones.filter(z=>z.kind==='keepout')
                   .map(z=>({pts:z.pts.map(p=>[Math.round(p[0]),Math.round(p[1])])})),""",
    "polygons in the export")

rep("""    zones=Array.isArray(p.zones)?p.zones.filter(z=>z&&(z.kind==='floor'||z.kind==='keepout'))
      .map(z=>({kind:z.kind,x0:+z.x0,y0:+z.y0,x1:+z.x1,y1:+z.y1})):[];
    zsel=-1; zdraw=null; zmode='';""",
    """    zones=Array.isArray(p.zones)?p.zones
      .filter(z=>z&&(z.kind==='floor'||z.kind==='keepout'))
      .map(z=>z.pts ? {kind:z.kind,pts:z.pts.map(q=>[+q[0],+q[1]])}
                    : {kind:z.kind,pts:[[+z.x0,+z.y0],[+z.x1,+z.y0],   // an old rectangle
                                        [+z.x1,+z.y1],[+z.x0,+z.y1]]}) :[];
    zsel=-1; zdraft=null; zmode=''; zvert=-1;""",
    "load polygons, upgrading old rectangles")

# Escape cancels a half-drawn shape
rep("""  if(e.key===' '){e.preventDefault();timer?stopT():play()}});""",
    """  if(e.key==='Escape'&&(zdraft||zmode)){ e.preventDefault(); zdraft=null; zmode='';
    $('zFloor').classList.remove('pri'); $('zKeep').classList.remove('pri'); render(); return }
  if(e.key==='Enter'&&zdraft){ e.preventDefault(); closeZone(); return }
  if(e.key===' '){e.preventDefault();timer?stopT():play()}});""",
    "escape and enter")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
