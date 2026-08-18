"""The stage centres itself, scales with the monitor, and can fill the window.

Jon: "can the scene be centered and have auto scale with the monitor plus a max screen icon that
fills the window?"

  - the canvas loses its 660px cap: it centres in its panel and grows with the column, so a big
    monitor gets a big room (4:3 held, pixels stay crisp)
  - a fullscreen button on the stage: the canvas fills the window, Esc or the button leaves;
    the transport stays reachable underneath
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


rep("""  canvas{image-rendering:pixelated;display:block;border-radius:6px;background:#0a0508;
         width:100%;height:auto;max-width:660px}""",
    """  canvas{image-rendering:pixelated;display:block;border-radius:6px;background:#0a0508;
         width:100%;height:auto}
  /* the stage: centred, growing with the monitor rather than capped at 660px */
  .stagebox{position:relative;display:flex;justify-content:center}
  .stagebox canvas{max-width:min(100%, calc((100vh - 220px) * 4 / 3))}
  .fsBtn{position:absolute;top:8px;right:8px;z-index:3;padding:4px 9px;opacity:.85}
  .fsBtn:hover{opacity:1}
  /* filling the window: the stage panel takes over, everything else waits */
  .fs .stagebox canvas{max-width:min(100vw - 40px, calc((100vh - 120px) * 4 / 3))}
  body.fs main{grid-template-columns:1fr;grid-template-areas:"stage"}
  body.fs .col-room, body.fs .col-pet, body.fs .gut{display:none}""",
    "centred and monitor-scaled")

rep("""    <canvas id="view" width="960" height="720"></canvas>""",
    """    <div class="stagebox">
      <button class="mini fsBtn" id="fsBtn" title="fill the window (Esc leaves)">&#x26F6;</button>
      <canvas id="view" width="960" height="720"></canvas>
    </div>""",
    "the canvas sits in a centring box with the button")

rep("""$('undoB').onclick=()=>undoStep();""",
    """$('fsBtn').onclick=()=>{ document.body.classList.toggle('fs');
  $('fsBtn').innerHTML=document.body.classList.contains('fs')?'&#x2716;':'&#x26F6;'; };
addEventListener('keydown',e=>{ if(e.key==='Escape'&&document.body.classList.contains('fs')){
  document.body.classList.remove('fs'); $('fsBtn').innerHTML='&#x26F6;'; } });
$('undoB').onclick=()=>undoStep();""",
    "fill the window, Esc leaves")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
