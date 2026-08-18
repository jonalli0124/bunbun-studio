"""Drag him into place, and stop the two buttons pretending to be each other.

"when i hit play it doesnt walk around and then sit it just does the animation" - because Play is
the render loop and Preview is the sim, and nothing about "Play" said so. Renamed:

    Play     -> animation: running / paused     (the frames tick over)
    Preview  -> Act it out                      (he walks to things and uses them)

And placement becomes what it should always have been: grab the ghost and put him where he goes.
The inferred offset is a starting point; the drag is the answer.
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


# ---------------------------------------------------------------- the readout
rep("""        <div style="margin-top:8px">
          <label>drawn at <span id="asl">100</span>%""",
    """        <div style="margin-top:8px">
          <label>where he sits on it
            <button class="mini" id="apReset" style="float:right">centre</button></label>
          <div class="hint" id="apNote">drag him in the room to place him</div>
        </div>
        <div style="margin-top:8px">
          <label>drawn at <span id="asl">100</span>%""",
    "a placement readout")

rep("""  { const sc=(a.stage&&a.stage.scale)||100;""",
    """  { const pl=a.place||{dx:0,dy:0};
    const d=(pl.dx||pl.dy) ? `offset ${pl.dx>0?'+':''}${pl.dx}, ${pl.dy>0?'+':''}${pl.dy}`
                           : 'right on the object';
    $('apNote').textContent = d+' \\u2014 '+(pl.from||'drag him to move it'); }
  { const sc=(a.stage&&a.stage.scale)||100;""",
    "say where he is placed")

rep("""$('ascale').oninput=e=>{""",
    """$('apReset').onclick=()=>{ const a=anims[asel]; if(!a)return;
  a.place={dx:0,dy:0,from:'centred by hand'}; syncRule(); draw() };
$('ascale').oninput=e=>{""",
    "recentre")

# ---------------------------------------------------------------- dragging the ghost
rep("""  const hit=pickObj(q);""",
    """  // The ghost is grabbable when its animation is selected. Objects win a tie only if the click
  // is not on him - placing him on top of a chair would otherwise be impossible.
  const ga=anims[asel];
  if(ga && ga.on && !simOn){
    for(const gp of ghostSpots(ga)){
      if(Math.abs(q.x-gp.x)<=14 && q.y<=gp.y+4 && q.y>=gp.y-42){
        ga.place=ga.place||{dx:0,dy:0};
        drag={anim:ga, dx:ga.place.dx-q.x, dy:ga.place.dy-q.y};
        cv.setPointerCapture(e.pointerId); draw(); return } } }
  const hit=pickObj(q);""",
    "grab the ghost")

rep("""  if(drag && osel>=0){ const o=scene.objects[osel]; o.x=q.x+drag.dx; o.y=q.y+drag.dy; draw() }""",
    """  if(drag && drag.anim){ const p=drag.anim.place;
    p.dx=Math.round(q.x+drag.dx); p.dy=Math.round(q.y+drag.dy);
    p.from='placed by hand'; syncRule(); draw(); return }
  if(drag && osel>=0){ const o=scene.objects[osel]; o.x=q.x+drag.dx; o.y=q.y+drag.dy; draw() }""",
    "drag him")

# every place the selected animation is currently drawn, so the grab test knows where to look
rep("""// WHERE an animation may actually happen in THIS room.""",
    """// Every point the selected animation is drawn at right now - the same maths the ghosts use, so
// what you can grab is exactly what you can see.
function ghostSpots(a){
  if(!a) return [];
  const p=placesFor(a), ox=a.place?a.place.dx:0, oy=a.place?a.place.dy:0;
  if(p.kind==='anywhere'){
    const fl=scene.zones.find(z=>z.kind==='floor'&&z.pts&&z.pts.length>2);
    if(fl){ const xs=fl.pts.map(q=>q[0]), ys=fl.pts.map(q=>q[1]);
      return [{x:(Math.min(...xs)+Math.max(...xs))/2+ox, y:(Math.min(...ys)+Math.max(...ys))/2+oy}] }
    return [{x:GAME.w/2+ox, y:GAME.floor+oy}];
  }
  return p.spots.map(o=>({x:o.x+ox, y:o.y+oy}));
}

// WHERE an animation may actually happen in THIS room.""",
    "know where he is on screen")

# arrows move him when no object is selected
rep("""  if(nud && osel>=0){ const o=scene.objects[osel];
    const k=e.shiftKey?5:1; o.x+=nud[0]*k; o.y+=nud[1]*k; e.preventDefault(); draw(); return }""",
    """  if(nud && osel>=0){ const o=scene.objects[osel];
    const k=e.shiftKey?5:1; o.x+=nud[0]*k; o.y+=nud[1]*k; e.preventDefault(); draw(); return }
  if(nud && osel<0 && anims[asel]){ const a=anims[asel];
    a.place=a.place||{dx:0,dy:0}; const k=e.shiftKey?5:1;
    a.place.dx+=nud[0]*k; a.place.dy+=nud[1]*k; a.place.from='placed by hand';
    e.preventDefault(); syncRule(); draw(); return }""",
    "arrows nudge him too")

# ---------------------------------------------------------------- the sim uses the same offset
rep("""  const to = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();""",
    """  // He WALKS to the object, then settles onto the offset - otherwise he would trudge to a point
  // in mid-air beside it.
  const off=c.a.place||{dx:0,dy:0};
  const to = c.spot ? {x:c.spot.x, y:c.spot.y} : someWalkablePoint();
  const settle = c.spot ? {x:c.spot.x+off.dx, y:c.spot.y+off.dy} : {x:to.x+off.dx, y:to.y+off.dy};""",
    "the sim settles onto the offset")

rep("""       face:0, walkClip:walkClipFor(c.a, to.x<from.x?-1:1), says:""",
    """       settle, face:0, walkClip:walkClipFor(c.a, to.x<from.x?-1:1), says:""",
    "carry the settle point")

rep("""    if(d<=step){ sim.x=sim.tx; sim.y=sim.ty; sim.phase='play'; sim.t=0;""",
    """    if(d<=step){ sim.x=(sim.settle?sim.settle.x:sim.tx); sim.y=(sim.settle?sim.settle.y:sim.ty);
      sim.phase='play'; sim.t=0;""",
    "arrive onto the placed spot")

# ---------------------------------------------------------------- names that tell the truth
rep("""      <button id="preview">Preview</button>""",
    """      <button id="preview">Act it out</button>""",
    "rename Preview")

rep("""  $('preview').textContent=simOn?'Stop preview':'Preview';""",
    """  $('preview').textContent=simOn?'Stop':'Act it out';""",
    "and its running label")

rep("""$('play').onclick=()=>{ playing=!playing;
  $('play').textContent = playing ? '❮❮ running' : '▶ paused';""",
    """$('play').onclick=()=>{ playing=!playing;
  $('play').textContent = playing ? '❮❮ animation: running' : '▶ animation: paused';""",
    "rename Play")

rep("""$('play').textContent='❮❮ running';""",
    """$('play').textContent='❮❮ animation: running';""",
    "and its opening label")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
