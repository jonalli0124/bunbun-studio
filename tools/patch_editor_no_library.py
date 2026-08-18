"""The library goes; the NAME moves to the top of the editor.

Jon: "lets drop the library concept from the attach editor and move naming the animation to the
top so its obvious". The tool is one animation in, one animation out - so the flow is simply:

    name it at the top  ->  work on it  ->  save (anim_<name>_<stamp>.json)  ->  import that
                                            into the Scene Assembler

The library panel (keep / forget / the picker / the rule chooser) is hidden, not deleted:
its keydown handlers, storage keys and functions stay valid, and anything already kept in
localStorage stays where it is. The rule chooser goes with it - WHERE an animation may happen
was already decided in the assembler, so choosing it here was a leftover.

The name box moves to the very top, above the clip, where the file gets its name from.
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


# ---------------------------------------------------------------- hide the library panel
rep("""    <div class="panel">
      <label>library <span class="hint">&mdash; every animation you have saved</span></label>""",
    """    <div class="panel" style="display:none"><!-- RETIRED: the library lived here. The scene
      assembler imports saved FILES now, so keeping a second copy in browser storage was one
      more thing to explain. Hidden rather than deleted so old handlers and storage stay valid. -->
      <label>library <span class="hint">&mdash; every animation you have saved</span></label>""",
    "the library panel is retired")

# ---------------------------------------------------------------- the name moves to the top
rep("""    <div class="panel">
      <label>clip</label><select id="clip"></select>""",
    """    <div class="panel">
      <label>this animation is called
        <span class="hint">&mdash; the saved file is named after it</span></label>
      <input id="animName" placeholder="e.g. Bathing, silly play" style="width:100%;padding:8px;
        font:inherit;font-weight:600;border:1px solid var(--line);border-radius:8px;
        background:var(--bg);color:var(--ink);margin-bottom:10px">
      <label>clip</label><select id="clip"></select>""",
    "the name is the first thing on the page")

# the visible box drives the old hidden one, so exportStem() and everything else just works
rep("""const exportStem=()=>{
  const named=(($('libName')&&$('libName').value)||'').trim();""",
    """// The visible name box at the top IS the name; the old library field mirrors it so every
// export path that already reads libName keeps working unchanged.
(()=>{ const top=$('animName'), old=$('libName');
  if(top&&old){ top.value=old.value||'';
    top.oninput=()=>{ old.value=top.value };
    old.addEventListener('change',()=>{ top.value=old.value }); } })();
const exportStem=()=>{
  const named=(($('animName')&&$('animName').value)||($('libName')&&$('libName').value)||'').trim();""",
    "the top box is the name")

# saving without a name deserves a nudge, not a clip-named file
rep("""$('save').onclick=()=>flash('saved '+save(project(),`anim_${exportStem()}_${fileStamp()}.json`));""",
    """$('save').onclick=()=>{
  if(!(($('animName')&&$('animName').value)||'').trim())
    flash('tip: name it at the top and the file will be called that', false);
  flash('saved '+save(project(),`anim_${exportStem()}_${fileStamp()}.json`)) };""",
    "a nudge when unnamed")

# the project block reads as the one workflow now
rep("""      <label style="margin-top:12px">project <span class="hint">&mdash; what you set. Re-openable.</span></label>""",
    """      <label style="margin-top:12px">this animation
        <span class="hint">&mdash; save it as a file, load one to keep working, and import the
        saved file into the Scene Assembler</span></label>""",
    "save/load says what it is")

rep("""        <button class="pri" id="save">💾 save</button><button id="imp">📂 import</button>""",
    """        <button class="pri" id="save">💾 save animation</button><button id="imp">📂 load animation</button>""",
    "the buttons say it too")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
