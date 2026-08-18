"""An eye on every drawn shape: hide the DRAWING, keep the behaviour.

Jon: "for the areas i want a hide button for each one created". Distinct from on/off - an off
zone stops working; a hidden one keeps working and simply is not painted, so a finished layout
does not have to wear its scaffolding.
"""
import io, os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "scene_tool.html")
s = io.open(SRC, encoding="utf-8").read()
done = []
EYE = "\\u{1F441}"
BLIND = "\\u{1F648}"


def rep(old, new, label):
    global s
    if old not in s:
        print("  SKIP:", label)
        return
    s = s.replace(old, new, 1)
    done.append(label)
    print("  ok:", label)


rep('          <button class="mini" data-zren="${i}" title="name this shape">&#9998;</button>',
    '          <button class="mini" data-zviz="${i}" style="padding:0 4px" '
    'title="${z.viz===false?\'hidden - click to show\''
    ':\'hide the drawing - it still works, it is just not painted\'}"'
    '>${z.viz===false?\'' + BLIND + '\':\'' + EYE + '\'}</button>\n'
    '          <button class="mini" data-zren="${i}" title="name this shape">&#9998;</button>',
    "an eye on every zone row")

rep("    $('zList').querySelectorAll('[data-zren]').forEach(b=>b.onclick=ev=>{",
    "    $('zList').querySelectorAll('[data-zviz]').forEach(b=>b.onclick=ev=>{\n"
    "      ev.stopPropagation();\n"
    "      const z=scene.zones[+b.dataset.zviz]; z.viz=(z.viz===false)?true:false; draw() });\n"
    "    $('zList').querySelectorAll('[data-zren]').forEach(b=>b.onclick=ev=>{",
    "wired")

rep("""  for(const z of scene.zones){
    if(!z.pts||z.pts.length<3) continue;""",
    """  for(const z of scene.zones){
    if(!z.pts||z.pts.length<3) continue;
    if(z.viz===false) continue;         // the DRAWING hides; the zone still does its job""",
    "a hidden zone is not painted")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
