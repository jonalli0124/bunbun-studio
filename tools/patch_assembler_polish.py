"""Three fixes the preview exposed.

1. There is no Adult_Walk in this pack - only Baby_Crawl. Travelling adults were frozen in the
   pose of whatever they were about to do, which read as a bug. Idle is the honest stand-in until
   an adult walk is generated.
2. Start on the farmhouse: it is the empty room, which is the one you fill.
3. Say "the armchair", not "A_simple_wooden_armchair_seen".
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


rep("""function walkClipFor(a){
  const want=phaseOf(a.clip)==='baby' ? /baby.*(walk|crawl)/i : /adult.*walk/i;
  return Object.keys(SD.clips).find(k=>want.test(k)) || null;
}""",
    """// What he travels in. The pack ships Baby_Crawl but no adult walk yet, so an adult travels in
// Idle - upright and breathing, which reads as movement - rather than sliding across the floor
// still folded into the pose he has not arrived to do yet.
function walkClipFor(a){
  const baby = phaseOf(a.clip)==='baby';
  const order = baby ? ['Baby_Crawl','Baby_Walk','Baby_Onesie']
                     : ['Adult_Walk','Adult_Idle','Adult_Bored'];
  return order.find(k=>SD.clips[k]) || null;
}""",
    "travel clip falls back honestly")

# a readable name for anything in the room
rep("const ruleNeeds=a=>a.rule&&a.rule.needs;",
    """const ruleNeeds=a=>a.rule&&a.rule.needs;
// Asset filenames are how the art was saved, not how a child would say it.
const nice=n=>{ const w=String(n).replace(/[_-]+/g,' ').replace(/\\.(png|gif)$/i,'')
    .replace(/\\b(seen|from|front|side|view|a|an|the|simple|flat|detailed)\\b/gi,' ')
    .replace(/\\s+/g,' ').trim();
  return w || String(n) };""",
    "readable object names")

for a, b, lbl in [
    ("`walking to the ${c.spot.object}`", "`walking to the ${nice(c.spot.object)}`",
     "status: walking to"),
    ("`${sim.a.name} at the ${sim.spot.object}`",
     "`${sim.a.name} at the ${nice(sim.spot.object)}`", "status: playing at"),
]:
    rep(a, b, lbl)

rep("""}\">${o.object}
        ${(o.can||[]).length?""",
    """}\">${nice(o.object)}
        ${(o.can||[]).length?""",
    "object list reads plainly")

rep("$('objName').textContent=o.object;",
    "$('objName').textContent=nice(o.object);",
    "selected object name")

rep("""        ? 'Places that afford it: '+p.spots.map(o=>`${o.object} (${Math.round(o.x)},${Math.round(o.y)})`).join(', ')""",
    """        ? (p.spots.length===1 ? 'One place affords it: ' : `${p.spots.length} places afford it: `)+
          p.spots.map(o=>`${nice(o.object)} at ${Math.round(o.x)}`).join(', ')+
          (p.spots.length>1 ? ' \\u2014 he picks one each time.' : '')""",
    "spell out every place")

# open on the empty room
rep("""$('room').innerHTML=Object.keys(SD.rooms).map(k=>`<option>${k}</option>`).join('');
scene.room=$('room').value;""",
    """$('room').innerHTML=Object.keys(SD.rooms).map(k=>`<option>${k}</option>`).join('');
// The farmhouse is the empty one, so it is where a scene of your own starts.
if(SD.rooms.farmhouse) $('room').value='farmhouse';
scene.room=$('room').value;""",
    "default to the farmhouse")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
