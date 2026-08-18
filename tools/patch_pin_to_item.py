"""Pin an animation to ONE item, and finish the WHERE taxonomy.

Jon: "the options would be anywhere he walks, pinned to a specific item or item that can have
that action or a bound area". Four answers to WHERE, and the panel now offers all four:

    anywhere he walks             wherever he can stand
    where anything affords X      every object marked X - two chairs are two destinations
    on one specific item          THIS chair, and no other
    in a drawn area               inside an activity area

Objects get a lasting id when they are added, so "this chair" survives reordering, deletion of
its neighbours, and a save/reload.
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


# ---------------------------------------------------------------- lasting ids
rep("""let spotSeq=1;""",
    """let spotSeq=1, oidSeq=1;
// every object gets a number that never changes, so a rule can say THIS chair
function ensureOids(){ for(const o of scene.objects) if(o.oid==null){
  while(scene.objects.some(q=>q.oid===oidSeq)) oidSeq++;
  o.oid=oidSeq++; } }""",
    "objects get lasting ids")

rep("""  scene.objects.push({object:n, x:GAME.w/2, y:GAME.floor+12,""",
    """  ensureOids();
  scene.objects.push({object:n, x:GAME.w/2, y:GAME.floor+12,""",
    "on add")

rep("""  const named=scene.objects.filter(o=>(o.can||[]).length).length;
  const lamps=scene.objects.filter(o=>o.lamp).length;""",
    """  ensureOids();
  const named=scene.objects.filter(o=>(o.can||[]).length).length;
  const lamps=scene.objects.filter(o=>o.lamp).length;""",
    "on scene import")

# ---------------------------------------------------------------- the rule offers items
rep("""  $('rNeeds').innerHTML=""",
    """  ensureOids();
  { const pinnable=scene.objects.filter(o=>!o.hidden);
    $('rPin').innerHTML = pinnable.length
      ? '<option value="">on one specific item\\u2026</option>'+pinnable.map(o=>
          `<option value="${o.oid}"${a.rule&&a.rule.onItem===o.oid?' selected':''}>only on the ${nice(o.object)} (${o.oid})</option>`).join('')
      : '<option value="">nothing in the room yet</option>';
    $('rPin').disabled=!pinnable.length; }
  $('rNeeds').innerHTML=""",
    "the pin select fills")

rep("""          <select id="rArea" style="flex:1"></select>""",
    """          <select id="rPin" style="flex:1"></select>
          <select id="rArea" style="flex:1"></select>""",
    "its select")

rep("""$('rArea').onchange=e=>{ const a=anims[asel]; if(!a)return;""",
    """$('rPin').onchange=e=>{ const a=anims[asel]; if(!a)return;
  if(e.target.value) a.rule={onItem:+e.target.value};
  syncRule(); draw() };
$('rArea').onchange=e=>{ const a=anims[asel]; if(!a)return;""",
    "wire it")

# ---------------------------------------------------------------- the sim honours a pin
rep("""function placesFor(a){
  if(a.rule&&a.rule.inArea!=null){""",
    """function placesFor(a){
  if(a.rule&&a.rule.onItem!=null){
    ensureOids();
    const o=scene.objects.find(q=>q.oid===a.rule.onItem && !q.hidden);
    return {kind:'needs', spots:o?[o]:[]};
  }
  if(a.rule&&a.rule.inArea!=null){""",
    "a pinned animation goes to its item")

rep("""  $('rAny').classList.toggle('pri', !ruleNeeds(a) && !(a.rule&&a.rule.inArea!=null));""",
    """  $('rAny').classList.toggle('pri',
    !ruleNeeds(a) && !(a.rule&&(a.rule.inArea!=null||a.rule.onItem!=null)));""",
    "the buttons reflect it")

# the "nothing affords it" message must not fire for pins and areas
rep("""  const p=placesFor(a);
  $('ruleWhy').innerHTML = p.kind==='anywhere'""",
    """  const p=placesFor(a);
  if(a.rule&&a.rule.onItem!=null){
    const o=scene.objects.find(q=>q.oid===a.rule.onItem);
    $('ruleWhy').innerHTML = o
      ? (o.hidden?`The ${nice(o.object)} is hidden, so this cannot play until it is shown.`
                 :`Pinned to the ${nice(o.object)} (${o.oid}) \\u2014 that item and no other.`)
      : 'The item this was pinned to is gone \\u2014 pick another place.';
  } else if(a.rule&&a.rule.inArea!=null){
    const z=areaOf(a.rule.inArea);
    $('ruleWhy').innerHTML = z
      ? `Bound to area ${a.rule.inArea} \\u2014 he does this only inside it.`
      : 'The drawn area this was bound to is gone \\u2014 pick another place.';
  } else
  $('ruleWhy').innerHTML = p.kind==='anywhere'""",
    "the panel explains a pin")

# ---------------------------------------------------------------- persistence
rep("""      inArea: (a.rule&&a.rule.inArea!=null)?a.rule.inArea:null,""",
    """      inArea: (a.rule&&a.rule.inArea!=null)?a.rule.inArea:null,
      onItem: (a.rule&&a.rule.onItem!=null)?a.rule.onItem:null,""",
    "exported")

rep("""      opacity:o.opacity==null?100:o.opacity, can:o.can||null, locked:!!o.locked, hidden:!!o.hidden,""",
    """      opacity:o.opacity==null?100:o.opacity, can:o.can||null, locked:!!o.locked, hidden:!!o.hidden,
      oid:o.oid,""",
    "ids travel in the scene")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
