"""A show/hide beside every lock, for objects and animations alike.

Jon: "we should have a show hide lock button to make it easier", after "if they have 5 sit on top
of each other, that could be an issue".

Same pair the scene placer's list uses. HIDDEN means what it means there: the thing keeps
everything about itself and is simply not drawn - and the pet cannot see it either, so hiding the
chair is how you find out what he does with nowhere to sit. LOCKED means clicks pass over it.
"""
import io, os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "scene_tool.html")
s = io.open(SRC, encoding="utf-8").read()
done = []

EYE = "\\u{1F441}"        # written into the page as a JS escape
BLIND = "\\u{1F648}"
LOCK = "\\u{1F512}"
OPEN = "\\u{1F513}"


def rep(old, new, label):
    global s
    if old not in s:
        print("  SKIP:", label)
        return
    s = s.replace(old, new, 1)
    done.append(label)
    print("  ok:", label)


# ---------------------------------------------------------------- objects
rep("""        ><button class="mini" data-lock="${i}" title="${o.locked?'locked - click to unlock'
        :'lock it so clicks pass over it'}" style="padding:0 4px;margin-right:5px"
        >${o.locked?'""" + LOCK + """':'""" + OPEN + """'}</button>${nice(o.object)}""",
    """        ><button class="mini" data-hide="${i}" title="${o.hidden?'hidden - click to show'
        :'hide it - not drawn, and he cannot use it'}" style="padding:0 4px;margin-right:3px"
        >${o.hidden?'""" + BLIND + """':'""" + EYE + """'}</button
        ><button class="mini" data-lock="${i}" title="${o.locked?'locked - click to unlock'
        :'lock it so clicks pass over it'}" style="padding:0 4px;margin-right:5px"
        >${o.locked?'""" + LOCK + """':'""" + OPEN + """'}</button>${nice(o.object)}""",
    "an eye on every object")

rep("""  $('objs').querySelectorAll('[data-lock]').forEach(b=>b.onclick=e=>{""",
    """  $('objs').querySelectorAll('[data-hide]').forEach(b=>b.onclick=e=>{
    e.stopPropagation();
    const o=scene.objects[+b.dataset.hide]; o.hidden=!o.hidden;
    if(o.hidden && osel===+b.dataset.hide){ osel=-1; syncObj() }
    litCache.key=''; draw() });
  $('objs').querySelectorAll('[data-lock]').forEach(b=>b.onclick=e=>{""",
    "wire the object eyes")

rep("""  for(const o of items){
    if(charZ!=null && !charDrawn && (o.z==null?o.y:o.z) > charZ) paintChar();""",
    """  for(const o of items){
    if(charZ!=null && !charDrawn && (o.z==null?o.y:o.z) > charZ) paintChar();
    if(o.hidden) continue;                       // kept in every way, simply not drawn""",
    "hidden objects are not drawn")

rep("""  for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i]; if(o.locked) continue;""",
    """  for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i]; if(o.locked||o.hidden) continue;""",
    "nor clickable")

rep("const lampItems=()=>scene.objects.filter(p=>p.lamp);",
    "const lampItems=()=>scene.objects.filter(p=>p.lamp&&!p.hidden);",
    "a hidden lamp gives no light")

rep("""    const spots=scene.objects.filter(o=>(o.can||[]).includes(a.rule.needs));""",
    """    // hiding the chair is how you find out what he does with nowhere to sit
    const spots=scene.objects.filter(o=>!o.hidden && (o.can||[]).includes(a.rule.needs));""",
    "and he cannot use it")

# ---------------------------------------------------------------- animations
rep("""      <button class="mini" data-alock="${i}" title="${a.locked?'locked - click to unlock'
        :'lock it so clicks pass over it'}" style="padding:0 4px"
        >${a.locked?'""" + LOCK + """':'""" + OPEN + """'}</button>""",
    """      <button class="mini" data-ahide="${i}" title="${a.hidden?'ghost hidden - click to show'
        :'hide its ghost'}" style="padding:0 4px"
        >${a.hidden?'""" + BLIND + """':'""" + EYE + """'}</button>
      <button class="mini" data-alock="${i}" title="${a.locked?'locked - click to unlock'
        :'lock it so clicks pass over it'}" style="padding:0 4px"
        >${a.locked?'""" + LOCK + """':'""" + OPEN + """'}</button>""",
    "an eye on every animation")

rep("""  $('anims').querySelectorAll('[data-alock]').forEach(b=>b.onclick=e=>{""",
    """  $('anims').querySelectorAll('[data-ahide]').forEach(b=>b.onclick=e=>{
    e.stopPropagation();
    const a=anims[+b.dataset.ahide]; a.hidden=!a.hidden; draw() });
  $('anims').querySelectorAll('[data-alock]').forEach(b=>b.onclick=e=>{""",
    "wire the animation eyes")

rep("""  if(a && a.on && !simOn && showGhosts){""",
    """  if(a && a.on && !a.hidden && !simOn && showGhosts){""",
    "a hidden ghost is not drawn")

rep("""    const a=anims[i]; if(!a.on || i!==asel) continue;""",
    """    const a=anims[i]; if(!a.on || a.hidden || i!==asel) continue;""",
    "nor clickable")

# ---------------------------------------------------------------- both persist
rep("""                          savedAt:a.savedAt, fromFile:a.fromFile})),""",
    """                          savedAt:a.savedAt, fromFile:a.fromFile, hidden:a.hidden})),""",
    "kept across a refresh")

rep("""    if(a.fromFile) made.fromFile=a.fromFile;""",
    """    if(a.fromFile) made.fromFile=a.fromFile;
    if(a.hidden) made.hidden=true;""",
    "and restored")

rep("""      can:o.can||null, locked:!!o.locked,""",
    """      can:o.can||null, locked:!!o.locked, hidden:!!o.hidden,""",
    "imports keep it too")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
