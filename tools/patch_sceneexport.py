"""Let the exports include the room.

Until now GIF/PNG/ZIP rendered only the character and the objects attached to it, on
transparency — so a scene staged in a room exported as a floating capybara. This adds a
"game scene" render mode that draws exactly what the in-game preview shows: room, room
objects and character at the real sprite scale. The baked JSON gains the same context.
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


# --- one painter, used by the live preview and by the exporter ---
rep("""function drawGame(fi,seq){
  const wrap=$('gamewrap');
  if(!DATA.rooms || !gRoom || !DATA.rooms[gRoom]){ wrap.style.display='none'; return }
  wrap.style.display='';
  const cv=$('gameview'), g=cv.getContext('2d'), Zg=GAME.z;
  cv.width=GAME.w*Zg; cv.height=GAME.h*Zg;
  g.imageSmoothingEnabled=false;
  g.clearRect(0,0,cv.width,cv.height);
  const rm=imgs['room|'+gRoom];
  if(rm&&rm.complete) g.drawImage(rm,0,0,cv.width,cv.height);""",
"""function paintScene(g,fi,seq,Zg,showSel){
  g.imageSmoothingEnabled=false;
  g.clearRect(0,0,GAME.w*Zg,GAME.h*Zg);
  const rm=imgs['room|'+gRoom];
  if(rm&&rm.complete) g.drawImage(rm,0,0,GAME.w*Zg,GAME.h*Zg);""",
    "extract paintScene")

rep("""  $('gsv').textContent=Math.round(sc*100);
  $('gxv').textContent=gX; $('gyv').textContent=gY;
  $('gx').value=gX; $('gy').value=gY;
  $('gsnote').textContent=` (game uses ${defaultScale()}% for the ${guessPhase()})`;
  rpDraw();
}""",
"""}
function drawGame(fi,seq){
  const wrap=$('gamewrap');
  if(!DATA.rooms || !gRoom || !DATA.rooms[gRoom]){ wrap.style.display='none'; return }
  wrap.style.display='';
  const cv=$('gameview'), g=cv.getContext('2d'), Zg=GAME.z;
  cv.width=GAME.w*Zg; cv.height=GAME.h*Zg;
  paintScene(g,fi,seq,Zg,true);
  const sc=(gScale==null?defaultScale():gScale)/100;
  $('gsv').textContent=Math.round(sc*100);
  $('gxv').textContent=gX; $('gyv').textContent=gY;
  $('gx').value=gX; $('gy').value=gY;
  $('gsnote').textContent=` (game uses ${defaultScale()}% for the ${guessPhase()})`;
  rpDraw();
}""",
    "drawGame wraps paintScene")

# the selection ring belongs to the editor, never to an export
rep("  const items=rprops.map((p,i)=>({y:p.y,go:()=>drawRoomProp(g,p,Zg,i===rsel)}));",
    "  const items=rprops.map((p,i)=>({y:p.y,go:()=>drawRoomProp(g,p,Zg,showSel&&i===rsel)}));",
    "no selection ring in exports")
rep("""  g.strokeStyle='rgba(255,255,255,.18)'; g.lineWidth=1;      // the floor line, for reference
  g.beginPath(); g.moveTo(0,GAME.floor*Zg+.5); g.lineTo(cv.width,GAME.floor*Zg+.5); g.stroke();""",
    """  if(showSel){                                              // the floor line, editor only
    g.strokeStyle='rgba(255,255,255,.18)'; g.lineWidth=1;
    g.beginPath(); g.moveTo(0,GAME.floor*Zg+.5); g.lineTo(GAME.w*Zg,GAME.floor*Zg+.5); g.stroke(); }""",
    "no floor line in exports")

# --- a third crop mode: the whole scene ---
rep("""function composeFrame(fi,seq,scale,crop){
  const w=(crop==='frame'?S:FULL)*scale;""",
"""function composeFrame(fi,seq,scale,crop){
  if(crop==='scene'){                       // exactly what the in-game preview shows
    const cv=document.createElement('canvas');
    cv.width=GAME.w*scale; cv.height=GAME.h*scale;
    paintScene(cv.getContext('2d'),fi,seq,scale,false);
    return cv;
  }
  const w=(crop==='frame'?S:FULL)*scale;""",
    "scene crop mode")

rep("""          <option value="pad" selected>padded</option><option value="frame">96&times;96</option>""",
    """          <option value="pad" selected>padded</option><option value="frame">96&times;96</option>
            <option value="scene">game scene 320&times;240</option>""",
    "scene option in the picker")

# --- the baked JSON should say where this was staged ---
rep("    rest:c.rest,characterZ:ci,character:{...CH},",
    """    rest:c.rest,characterZ:ci,character:{...CH},
    scene:{room:gRoom||null, width:GAME.w, height:GAME.h, floor:GAME.floor,
           character:{x:gX, y:gY, scalePercent:(gScale==null?defaultScale():gScale)},
           objects:rprops.map(p=>({object:p.prop, x:p.x, y:p.y,
             scale:+p.scale.toFixed(3), rotation:p.rot, flip:p.flip,
             opacity:p.opac==null?100:p.opac,
             width:Math.round(DATA.props[p.prop].w*p.scale),
             height:Math.round(DATA.props[p.prop].h*p.scale)}))},""",
    "scene block in the baked export")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written:", len(s), "bytes (was", len(orig), ")")
