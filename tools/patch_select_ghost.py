"""Click him. That is the whole feature.

Until now the ghost could only be grabbed once its animation was already highlighted in the list,
and the grab area was a fixed 28x46 box I guessed at - so it neither matched his outline nor
followed the "drawn at %" slider. Clicking the capybara did nothing, or selected the chair behind
him, which is exactly "how do i select just the ghost?".

Now: clicking him selects HIS animation and starts the drag, whatever was selected before, using
his real drawn bounds from the frame's bbox. He shows a dashed outline when selected and the
cursor changes over him, so it is visible that he is a thing you can pick up.
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


# ---------------------------------------------------------------- his real outline
rep("""// WHERE an animation may actually happen in THIS room. An animation that needs a bath and finds""",
    """// His actual drawn extent at a spot, from the frame's own bbox - NOT a guessed box. drawChar
// paints the 96px sprite at (x-48*sc, y-90*sc), so the bbox maps straight into the room.
function ghostBounds(a, spot){
  if(!a || !SD.clips[a.clip]) return null;
  const sc=SCALE[phaseOf(a.clip)]*(((a.stage&&a.stage.scale)||100)/100);
  const f=SD.clips[a.clip].frames[(a.order&&a.order[0]?a.order[0].i:0)] || {};
  const bb=f.bbox||[24,20,72,90];
  return {l:spot.x+(bb[0]-48)*sc, r:spot.x+(bb[2]-48)*sc,
          t:spot.y+(bb[1]-90)*sc, b:spot.y+(bb[3]-90)*sc};
}
// Which animation, if any, is under this point. Searched from the top of the list down, so the
// one drawn last is the one you grab.
function pickGhost(q){
  if(simOn) return null;
  for(let i=anims.length-1;i>=0;i--){
    const a=anims[i]; if(!a.on) continue;
    for(const sp of ghostSpots(a)){
      const b=ghostBounds(a,sp);
      if(b && q.x>=b.l-2 && q.x<=b.r+2 && q.y>=b.t-2 && q.y<=b.b+2) return {i, a, sp};
    }
  }
  return null;
}

// WHERE an animation may actually happen in THIS room. An animation that needs a bath and finds""",
    "his real bounds, and what is under the cursor")

# ---------------------------------------------------------------- clicking him
rep("""  // The ghost is grabbable when its animation is selected. Objects win a tie only if the click
  // is not on him - placing him on top of a chair would otherwise be impossible.
  const ga=anims[asel];
  if(ga && ga.on && !simOn){
    for(const gp of ghostSpots(ga)){
      if(Math.abs(q.x-gp.x)<=14 && q.y<=gp.y+4 && q.y>=gp.y-42){
        ga.place=ga.place||{dx:0,dy:0};
        drag={anim:ga, dx:ga.place.dx-q.x, dy:ga.place.dy-q.y};
        cv.setPointerCapture(e.pointerId); draw(); return } } }""",
    """  // He is checked BEFORE objects: he is usually standing on one, and if the chair won the tie
  // you could never pick him up. Clicking him also selects his animation, so there is no
  // "select it in the list first" step.
  const gh=pickGhost(q);
  if(gh){
    asel=gh.i; osel=-1; zsel=-1; syncObj(); syncRule();
    gh.a.place=gh.a.place||{dx:0,dy:0};
    drag={anim:gh.a, dx:gh.a.place.dx-q.x, dy:gh.a.place.dy-q.y};
    cv.setPointerCapture(e.pointerId); draw(); return; }""",
    "clicking him selects and grabs him")

# clicking empty space should let go of him too
rep("""  osel=-1; syncObj(); draw();
});""",
    """  osel=-1; asel=-1; syncObj(); syncRule(); draw();
});""",
    "clicking away lets go")

# ---------------------------------------------------------------- show that he is selectable
rep("""cv.addEventListener('pointerup',()=>{ drag=null; zvert=-1 });""",
    """cv.addEventListener('pointerup',()=>{ drag=null; zvert=-1 });
// the cursor is the only hint you get before you click, so make it tell the truth
cv.addEventListener('pointermove',e=>{ if(drag||zmode) return;
  cv.style.cursor = pickGhost(at(e)) ? 'grab' : (pickObj(at(e))>=0 ? 'move' : 'default') });""",
    "the cursor says he is grabbable")

rep("""      ctx.strokeStyle='#e8392d'; ctx.lineWidth=2; ctx.setLineDash([4,3]);
      ctx.beginPath(); ctx.ellipse(x*Z,y*Z,9*Z,3*Z,0,0,7); ctx.stroke(); ctx.setLineDash([]); };""",
    """      ctx.strokeStyle='#e8392d'; ctx.lineWidth=2; ctx.setLineDash([4,3]);
      ctx.beginPath(); ctx.ellipse(x*Z,y*Z,9*Z,3*Z,0,0,7); ctx.stroke(); ctx.setLineDash([]);
      // an outline round the selected one, so "which of these am I dragging" is never a guess
      if(anims[asel]===a){ const b=ghostBounds(a,{x,y});
        if(b){ ctx.strokeStyle='#9151d3'; ctx.lineWidth=2; ctx.setLineDash([5,4]);
          ctx.strokeRect(b.l*Z-2,b.t*Z-2,(b.r-b.l)*Z+4,(b.b-b.t)*Z+4); ctx.setLineDash([]) } } };""",
    "outline the one you have hold of")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
