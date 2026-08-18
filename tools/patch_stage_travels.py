"""Let the authored size leave the editor, and show it once it arrives.

`animCore()` - what the editor writes into the library - listed five fields and `stage` was not
one of them. That omission was deliberate for the editor's own re-apply (loading an animation
should not shove your staged character around), but it meant the size you authored never left the
building. "sit in the chair" was staged at 63% and arrived in the assembler as 100%.

Two changes:
  editor    animCore() now carries `stage`. applyAnim() still ignores it, so re-applying an
            animation in the editor behaves exactly as before.
  assembler each animation gets a size, defaulting to whatever it was authored at. Entries saved
            BEFORE this patch have no stage at all, so the control is also the way to fix them
            without re-exporting.
"""
import io, os

HERE = os.path.dirname(os.path.abspath(__file__))
done = []


def patch(path, edits):
    s = io.open(path, encoding="utf-8").read()
    for old, new, label in edits:
        if old not in s:
            print("  SKIP:", label)
            continue
        s = s.replace(old, new, 1)
        done.append(label)
        print("  ok:", label)
    tmp = path + ".tmp"
    io.open(tmp, "w", encoding="utf-8").write(s)
    os.replace(tmp, path)


patch(os.path.join(HERE, "src", "attach_editor.html"), [
    ("""function animCore(){
  const p=projectCore();
  return {clip:p.clip, fps:p.fps, character:p.character, order:p.order, layers:p.layers};
}""",
     """function animCore(){
  const p=projectCore();
  // `stage` rides along so the size and depth you authored survive the trip to the assembler.
  // applyAnim below still ignores it - re-applying here must not move your staged character.
  return {clip:p.clip, fps:p.fps, character:p.character, order:p.order, layers:p.layers,
          stage:p.stage};
}""",
     "editor: the library keeps the authored stage"),
])

patch(os.path.join(HERE, "src", "scene_tool.html"), [
    # a size control per animation, seeded from what it was authored at
    ("""      <div class="row">
        <button class="mini" id="rAny">anywhere he walks</button>
        <select id="rNeeds"></select>
      </div>""",
     """      <div class="row">
        <button class="mini" id="rAny">anywhere he walks</button>
        <select id="rNeeds"></select>
      </div>
      <div style="margin-top:8px">
        <label>drawn at <span id="asl">100</span>%
          <span class="hint" id="asNote"></span></label>
        <input type="range" id="ascale" min="20" max="200" step="1" value="100" style="width:100%">
      </div>""",
     "assembler: a size for each animation"),

    ("""  $('rAny').classList.toggle('pri', !ruleNeeds(a));""",
     """  { const sc=(a.stage&&a.stage.scale)||100;
    $('ascale').value=Math.round(sc); $('asl').textContent=Math.round(sc);
    $('asNote').textContent = a.stage && a.stage.authored
      ? '\\u2014 the size you staged it at in the editor'
      : '\\u2014 this one was saved before sizes travelled; set it here'; }
  $('rAny').classList.toggle('pri', !ruleNeeds(a));""",
     "show where the size came from"),

    ("""$('rAny').onclick=()=>{ const a=anims[asel]; if(!a)return; a.rule={anywhere:true}; syncRule(); draw() };""",
     """$('ascale').oninput=e=>{ const a=anims[asel]; if(!a)return;
  a.stage=a.stage||{}; a.stage.scale=+e.target.value;
  $('asl').textContent=e.target.value; draw() };
$('rAny').onclick=()=>{ const a=anims[asel]; if(!a)return; a.rule={anywhere:true}; syncRule(); draw() };""",
     "wire the size"),

    # remember whether the number was authored or typed here
    ("""  const st=p.stage||{};
  const a={name, clip, rule, on:true, fps:p.fps||7,
    stage:{x:+st.x||null, y:+st.y||null, z:+st.z||null,
           scale:(typeof st.scale==='number'&&st.scale>0)?st.scale:100},""",
     """  const st=p.stage||{};
  const authored=(typeof st.scale==='number'&&st.scale>0);
  const a={name, clip, rule, on:true, fps:p.fps||7,
    stage:{x:+st.x||null, y:+st.y||null, z:+st.z||null,
           scale:authored?st.scale:100, authored},""",
     "note whether the size was authored"),
])

print("written;", len(done), "edits")
