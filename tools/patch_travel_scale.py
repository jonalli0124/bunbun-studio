"""Two scales, because two different things need scaling and only one of them is authored.

An animation you MADE carries its own size: the editor writes a per-frame character scale (`cs`)
into every entry of its order, so "sit in the chair" arrives already sized to that chair and
nothing here should second-guess it.

What travels between animations was never authored. Walking and idling are clips the assembler
reaches for on its own, and they render at the raw game scale - which is why the capybara loomed
over two armchairs he was on his way to sit in. That needs its own dial:

  everything passive is drawn at   -> the scenery (props)
  he travels at                    -> the character while walking or idling

Both fold into the export; both keep the authored numbers alongside, so nothing is lost.
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


rep("""      <input type="range" id="ps" min="40" max="200" step="5" value="100" style="width:100%">
      <div class="hint">one scale for the scenery, so props authored at different sizes still
        agree with each other. Each object keeps its own relative size underneath.</div>""",
    """      <input type="range" id="ps" min="40" max="200" step="5" value="100" style="width:100%">
      <div class="hint">one scale for the scenery, so props authored at different sizes still
        agree with each other. Each object keeps its own relative size underneath.</div>

      <label style="margin-top:10px">he travels at
        <span id="tsl" style="color:var(--ink);font-weight:600">100%</span>
        <button class="mini" id="tsReset" style="float:right">reset</button></label>
      <input type="range" id="ts" min="30" max="200" step="5" value="100" style="width:100%">
      <div class="hint">walking and idling only. Your animations bring their own size from the
        editor, so this never touches them &mdash; it is here because nobody authored the walk.</div>""",
    "the travel scale control")

rep("let passiveScale=1;",
    """let passiveScale=1;
// The character's size while doing something nobody authored - travelling between things.
let travelScale=1;""",
    "travel scale state")

# drawChar takes clipOver only for travel/idle, so that is exactly where it applies
rep("""  const clip = clipOver || a.clip;
  const sc=SCALE[phaseOf(clip)];""",
    """  const clip = clipOver || a.clip;
  // clipOver is only ever set for a clip the assembler chose itself (walking, idling). An
  // animation you authored keeps the size the editor gave it, untouched.
  const sc=SCALE[phaseOf(clip)] * (clipOver ? travelScale : 1);""",
    "travel scale applies only to unauthored clips")

rep("""$('psReset').onclick=()=>{ passiveScale=1; $('ps').value=100; $('psl').textContent='100%'; draw() };""",
    """$('psReset').onclick=()=>{ passiveScale=1; $('ps').value=100; $('psl').textContent='100%'; draw() };
$('ts').oninput=e=>{ travelScale=+e.target.value/100;
  $('tsl').textContent=e.target.value+'%'; draw() };
$('tsReset').onclick=()=>{ travelScale=1; $('ts').value=100; $('tsl').textContent='100%'; draw() };""",
    "wire the travel scale")

rep("""    passiveScale: +passiveScale.toFixed(3),""",
    """    passiveScale: +passiveScale.toFixed(3),
    travelScale: +travelScale.toFixed(3),""",
    "travel scale travels with the scene")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
