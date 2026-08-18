"""GAME VIEW: the scene inside the shipped 240x320 chrome, ported from the builder.

Jon: "you have the full game and the builder had all of it inside of it" - correct, and the
chrome was never sprites: placer.html DRAWS it (chip row, clock, stat band, ticker, tab bar)
from the ui.h palette. Ported here as a toggle: the stage swaps to the portrait panel with the
live scene in the room window, the real to-the-minute clock, and the ticker showing what he is
doing right now. The CARE/SLEEP sheets and the pin row stay in the builder - this is the home
screen, which is what watching him needs.
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


rep("""      <canvas id="view" width="960" height="720"></canvas>""",
    """      <canvas id="view" width="960" height="720"></canvas>
      <canvas id="gview" width="480" height="640" style="display:none;max-width:min(100%,420px);
        margin:0 auto"></canvas>""",
    "the portrait canvas")

rep("""      <button id="marksBtn" title="the red spot under the ghost and the purple ring under things that afford an action">Circles: on</button>""",
    """      <button id="marksBtn" title="the red spot under the ghost and the purple ring under things that afford an action">Circles: on</button>
      <button id="gvBtn" title="the scene inside the shipped 240x320 chrome, drawn from ui.h - not sprites">Game view</button>""",
    "its button")

rep("""$('marksBtn').onclick=()=>{""",
    """// ---- GAME VIEW: the shipped chrome, drawn - ported from placer.html (itself read from
// ui.h / main.cpp). rr/pad/txt/tabBar/statBand/ticker are the builder's own painters.
let gameView=false;
const GC={ink:'#33322c',soft:'#6d685a',paper:'#f6f1e4',bone:'#e9e2d2',boneLo:'#cfc6b0',
  boneEdge:'#b3a98e',orange:'#e8501e',disc:'#8a6ee6',hp:'#27ae60',low:'#c0392b',
  moon:'#f0c84a',love:'#fd6bb0'};
const GTABS=['CARE','PLAY','MUSIC','SLEEP'];
const GSTAT_L=['FOOD','FUN','CLN','ZZZ','WORK','LOVE','HP'],
      GSTAT_V=[78,64,81,55,70,88,92],
      GSTAT_C=[GC.orange,GC.orange,GC.orange,GC.orange,GC.disc,GC.love,GC.hp];
function grr(g,x,y,w,h,r){g.beginPath();g.moveTo(x+r,y);g.arcTo(x+w,y,x+w,y+h,r);
  g.arcTo(x+w,y+h,x,y+h,r);g.arcTo(x,y+h,x,y,r);g.arcTo(x,y,x+w,y,r);g.closePath()}
function gpad(g,x,y,w,h,r,fill,edge){g.fillStyle=fill;grr(g,x,y,w,h,r);g.fill();
  if(edge){g.strokeStyle=edge;g.lineWidth=1;grr(g,x,y,w,h,r);g.stroke()}}
function gtxt(g,t,x,y,col,px,al){g.fillStyle=col;g.font=px+'px ui-monospace,Menlo,monospace';
  g.textAlign=al||'left';g.fillText(t,x,y);g.textAlign='left'}
function gtabIcon(g,i,cx,cy,d){const r=d/2;
  if(i===0){ // a drawn bowl (the builder used the feed sprite; a bowl reads the same)
    g.strokeStyle=GC.ink;g.lineWidth=1.5;
    g.beginPath();g.arc(cx,cy,r-1,0.1,Math.PI-0.1);g.stroke();
    g.beginPath();g.moveTo(cx-r+1,cy);g.lineTo(cx+r-1,cy);g.stroke();}
  else if(i===1){const gx=cx-r,gy=cy-r,sz=d;
    g.strokeStyle=GC.ink;g.lineWidth=1;g.beginPath();
    g.moveTo(gx+sz/3,gy);g.lineTo(gx+sz/3,gy+sz);g.moveTo(gx+2*sz/3,gy);g.lineTo(gx+2*sz/3,gy+sz);
    g.moveTo(gx,gy+sz/3);g.lineTo(gx+sz,gy+sz/3);g.moveTo(gx,gy+2*sz/3);g.lineTo(gx+sz,gy+2*sz/3);
    g.stroke();
    g.strokeStyle=GC.orange;g.beginPath();
    g.moveTo(gx+1,gy+1);g.lineTo(gx+sz/3-2,gy+sz/3-2);
    g.moveTo(gx+sz/3-2,gy+1);g.lineTo(gx+1,gy+sz/3-2);g.stroke();
    g.strokeStyle=GC.low;g.beginPath();g.arc(gx+5*sz/6,gy+5*sz/6,sz/8+1,0,7);g.stroke()}
  else if(i===2){
    g.fillStyle=GC.ink;g.beginPath();g.arc(cx-r/2+1,cy+r/2,r/3+1,0,7);g.fill();
    g.fillRect(cx-r/2+r/3+1,cy-r+2,2,r+r/2-1);
    g.strokeStyle=GC.ink;g.beginPath();g.moveTo(cx-r/2+r/3+2,cy-r+2);
    g.lineTo(cx+r-1,cy-r/2+1);g.stroke()}
  else{
    g.fillStyle=GC.moon;g.beginPath();g.arc(cx,cy,r,0,7);g.fill();
    g.fillStyle=GC.paper;g.beginPath();g.arc(cx+r/2,cy-r/3,r-1,0,7);g.fill()}}
function gtabBar(g,y,h){
  g.fillStyle=GC.bone;g.fillRect(0,y,240,320-y);
  const tall=h>=70,iconD=tall?28:16,iconCy=y+(tall?34:14),lblY=y+h-22;
  for(let i=0;i<4;i++){const x=4+i*59,cx=x+27;
    gpad(g,x,y,55,h,7,GC.paper,GC.ink);
    gtabIcon(g,i,cx,iconCy,iconD);
    gtxt(g,GTABS[i],cx,lblY+8,GC.ink,8,'center')}}
function gstatBand(g,sy){
  g.fillStyle=GC.paper;g.fillRect(0,sy,240,24);
  const w=Math.floor((240-6-2*6)/7);
  for(let i=0;i<7;i++){const x=3+i*(w+2);
    gtxt(g,GSTAT_L[i],x+1,sy+8,GC.soft,6);
    g.strokeStyle=GC.ink;g.lineWidth=1;g.strokeRect(x+0.5,sy+12.5,w,7);
    const f=Math.round((w-2)*GSTAT_V[i]/100);
    g.fillStyle=GSTAT_V[i]<20?GC.low:GSTAT_C[i];g.fillRect(x+1,sy+13,f,5)}}
function gticker(g,y,msg){g.fillStyle=GC.ink;g.fillRect(0,y,240,14);
  gtxt(g,String(msg||'').slice(0,44),4,y+10,GC.paper,8)}
function drawGameView(){
  const gvc=$('gview'), g=gvc.getContext('2d');
  g.imageSmoothingEnabled=false;
  g.setTransform(2,0,0,2,0,0);
  g.fillStyle=GC.bone;g.fillRect(0,0,240,320);
  // the room, unobstructed - the live stage scaled into the shipped window
  g.drawImage(cv,0,0,cv.width,cv.height,0,0,240,180);
  // the chip row at y4 (ui.h: PAUSE/DANCE/gear share y4)
  gpad(g,35,4,38,16,4,GC.boneLo,GC.boneEdge);
  gtxt(g,'PAUSE',54,15,GC.ink,7,'center');
  gtxt(g,(()=>{const h=Math.floor(todMin/60)%12||12;
    return h+':'+String(Math.floor(todMin)%60).padStart(2,'0')+(todMin>=720?'pm':'am')})(),
    54,26,nightAmount(todMin/60)>0.45?GC.bone:GC.ink,8,'center');
  gpad(g,81,4,78,24,4,GC.paper,GC.ink);
  gtxt(g,'DANCE',120,20,GC.ink,9,'center');
  gpad(g,166,4,26,24,5,GC.boneLo,null);
  g.strokeStyle=GC.soft;g.lineWidth=1;
  for(let i=0;i<6;i++){const a=i*Math.PI/3+0.52;
    g.beginPath();g.moveTo(179+Math.cos(a)*4,16+Math.sin(a)*4);
    g.lineTo(179+Math.cos(a)*8,16+Math.sin(a)*8);g.stroke()}
  g.beginPath();g.arc(179,16,5,0,7);g.stroke();
  g.beginPath();g.arc(179,16,2,0,7);g.stroke();
  gstatBand(g,182);
  gticker(g,210,(simOn&&sim&&sim.says)?sim.says.toUpperCase():'ADULT  ALL GOOD');
  gtabBar(g,228,88);
}
$('gvBtn').onclick=()=>{ gameView=!gameView;
  cv.style.display=gameView?'none':'';
  $('gview').style.display=gameView?'':'none';
  $('gvBtn').classList.toggle('pri',gameView);
  $('gvBtn').textContent=gameView?'Editor view':'Game view';
  draw(); if(gameView) drawGameView(); };
$('marksBtn').onclick=()=>{""",
    "the chrome, ported")

# the loop keeps the panel live
rep("""    lastT=now; draw() }""",
    """    lastT=now; draw(); if(gameView) drawGameView() }""",
    "and it stays live")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
