"""Global undo, names on every drawn shape, and a master switch for the zones.

Jon: "i want to be able to rename all of the different drawn areas. also i want a button for all
on or off or back to the way i had it. also need a global undo capability"

UNDO rides the machinery that already exists: session() is a complete description of the work,
and restore() already rebuilds the page from one - that is what a refresh does. So history is a
stack of sessions, captured by the same throttled autosave every change already triggers, and
undo is "restore the previous one". Ctrl+Z / Ctrl+Y, and buttons beside the transport.

Every zone gets the rename pencil (walkable and keep-outs too, not only areas), and the zone
list gets: all on / all off / as before - where "as before" restores the exact mix you had
before the last sweep.
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


# ---------------------------------------------------------------- global undo
rep("""function autosave(){ if(booting||saveTimer) return;""",
    """// ---- global undo: a stack of the same sessions the autosave writes ----
let hist=[], histAt=-1, applying=false;
function histPush(j){
  if(applying) return;
  if(histAt>=0 && hist[histAt]===j) return;
  hist=hist.slice(0, histAt+1); hist.push(j);
  if(hist.length>60) hist.shift();
  histAt=hist.length-1;
  histButtons();
}
function histButtons(){
  const u=$('undoB'), r=$('redoB');
  if(u){ u.disabled=histAt<1; u.style.opacity=histAt<1?.45:1 }
  if(r){ r.disabled=histAt>=hist.length-1; r.style.opacity=histAt>=hist.length-1?.45:1 }
}
async function applyHist(j){
  applying=true; booting=true;
  try{ await restore(j) }catch(e){ console.warn('undo failed', e) }
  booting=false; applying=false;
  try{ localStorage.setItem(SKEY, j) }catch(e){}
  syncRule(); syncObj(); draw(); histButtons();
}
async function undoStep(){ if(histAt>0){ histAt--; await applyHist(hist[histAt]) } }
async function redoStep(){ if(histAt<hist.length-1){ histAt++; await applyHist(hist[histAt]) } }
addEventListener('keydown',e=>{
  if(!(e.ctrlKey||e.metaKey)) return;
  if(/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
  const k=e.key.toLowerCase();
  if(k==='z' && !e.shiftKey){ e.preventDefault(); undoStep() }
  else if(k==='y' || (k==='z' && e.shiftKey)){ e.preventDefault(); redoStep() }
});
function autosave(){ if(booting||saveTimer) return;""",
    "history is a stack of sessions")

rep("""  saveTimer=setTimeout(()=>{ saveTimer=0;
    try{ localStorage.setItem(SKEY,JSON.stringify(session())) }catch(e){} }, 1000) }""",
    """  saveTimer=setTimeout(()=>{ saveTimer=0;
    const j=JSON.stringify(session());
    histPush(j);
    try{ localStorage.setItem(SKEY,j) }catch(e){} }, 1000) }""",
    "every saved change is an undo step")

rep("""      <button id="ghosts">Ghosts: on</button>""",
    """      <button id="ghosts">Ghosts: on</button>
      <button id="undoB" title="Ctrl+Z">&#8630;</button>
      <button id="redoB" title="Ctrl+Y">&#8631;</button>""",
    "the buttons")

rep("""$('ghosts').onclick=()=>{""",
    """$('undoB').onclick=()=>undoStep();
$('redoB').onclick=()=>redoStep();
$('ghosts').onclick=()=>{""",
    "wired")

# the opening state seeds the stack, so the first change is undoable
rep("""  booting=false;
  // A failed restore stays on disk. Saving now would overwrite the thing that failed to load.
  if(!failed) autosave();
  draw();
})();""",
    """  booting=false;
  // A failed restore stays on disk. Saving now would overwrite the thing that failed to load.
  histPush(JSON.stringify(session()));      // the opening state, so the first change can undo
  if(!failed) autosave();
  draw();
})();""",
    "the opening state seeds it")

# ---------------------------------------------------------------- rename any zone
rep("""          ${z.kind==='spot'?`<button class="mini" data-zren="${i}" title="name this area">&#9998;</button>`:''}""",
    """          <button class="mini" data-zren="${i}" title="name this shape">&#9998;</button>""",
    "a pencil on every shape")

rep("""      ? zs.map((z,i)=>{ const nm=z.kind==='floor'?'walkable'
          :z.kind==='spot'?(areaLabel(z)+(z.name?' (Z'+z.spotId+')':' area')):'keep out';""",
    """      ? zs.map((z,i)=>{ const nm=z.name?z.name+(z.kind==='spot'?' (Z'+z.spotId+')':'')
          :z.kind==='floor'?'walkable':z.kind==='spot'?('Z'+z.spotId+' area'):'keep out';""",
    "the name shows for any kind")

rep("""      ctx.fillText(spot?areaLabel(z):walk?'walkable':'keep out', cx*Z, cy*Z);""",
    """      ctx.fillText(z.name?z.name:(spot?areaLabel(z):walk?'walkable':'keep out'), cx*Z, cy*Z);""",
    "and on the canvas")

# ---------------------------------------------------------------- the master switch
rep("""        <button class="mini" id="zClose">finish</button>""",
    """        <button class="mini" id="zClose">finish</button>
        <button class="mini" id="zAllOn" title="switch every shape on">all on</button>
        <button class="mini" id="zAllOff" title="switch every shape off">all off</button>
        <button class="mini" id="zBack" title="back to the mix you had before the last sweep">as before</button>""",
    "all on / all off / as before")

rep("""$('zClose').onclick=closeZone;""",
    """let zoneMixBefore=null;
$('zAllOn').onclick=()=>{ zoneMixBefore=scene.zones.map(z=>z.on!==false);
  scene.zones.forEach(z=>z.on=true); syncRule(); draw() };
$('zAllOff').onclick=()=>{ zoneMixBefore=scene.zones.map(z=>z.on!==false);
  scene.zones.forEach(z=>z.on=false); syncRule(); draw() };
$('zBack').onclick=()=>{ if(!zoneMixBefore) return say('nothing swept yet',true);
  scene.zones.forEach((z,i)=>{ if(i<zoneMixBefore.length) z.on=zoneMixBefore[i] });
  syncRule(); draw() };
$('zClose').onclick=closeZone;""",
    "wired, remembering the mix")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
