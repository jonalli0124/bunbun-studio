"""The attach editor goes back to being a ONE-ANIMATION editor.

Jon: "i want to move the editor back to just a single animation edit" / "we need to strip a lot
of the room stuff since we moved that over to the scene" / "it also be treated as a 1 animation
editor that you can load or export".

The whole scene layer lives inside #gamewrap - the in-game view, time of day, weather, room
props, zones and lamps all grew in there. drawGame() shows it whenever a room is set, so the
strip is one decision: THAT BLOCK NEVER SHOWS. Every control inside it stays in the page,
hidden, so no JS reference breaks and an old project file still loads without error - its
roomObjects are simply carried, not edited here.

What remains is the animation itself: the clip, frames and holds, breathing, the stack of
attached props, the library, load and export. The room stays only as the backdrop behind the
sprite, because judging an animation against the room it will play in is part of authoring it.
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


rep("""function drawGame(fi,seq){
  const wrap=$('gamewrap');
  if(!DATA.rooms || !gRoom || !DATA.rooms[gRoom]){ wrap.style.display='none'; return }
  wrap.style.display='';""",
    """function drawGame(fi,seq){
  const wrap=$('gamewrap');
  // THE SCENE LAYER IS RETIRED FROM THIS TOOL. Rooms, placed objects, zones, lighting and
  // weather are the Scene Assembler's job now; this is a one-animation editor. The block and
  // all its controls stay in the page - hidden - so old project files still load, and their
  // roomObjects are carried untouched for the assembler to use.
  wrap.style.display='none'; return;
  if(!DATA.rooms || !gRoom || !DATA.rooms[gRoom]){ wrap.style.display='none'; return }
  wrap.style.display='';""",
    "the scene layer never shows")

rep("""      <label>clip</label><select id="clip"></select>
      <label style="margin-top:10px">room behind it</label><select id="room"></select>
      <button style="margin-top:10px;width:100%" id="demo">load bathe demo (5 layers)</button>""",
    """      <label>clip</label><select id="clip"></select>
      <label style="margin-top:10px">room behind it
        <span class="hint">&mdash; a backdrop to judge against; the room itself is built in the
        Scene Assembler</span></label><select id="room"></select>
      <button style="margin-top:10px;width:100%" id="demo">load bathe demo (5 layers)</button>""",
    "the backdrop says what it is")

rep("""<p>""",
    """<p class="eb" style="font-family:ui-monospace,Menlo,monospace;font-size:11px;
  letter-spacing:.14em;text-transform:uppercase;opacity:.75">one animation in &middot;
  one animation out &mdash; rooms are built in the Scene Assembler</p>
<p>""",
    "the page says what it is for")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
