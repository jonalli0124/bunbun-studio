"""Lock what you have finished with, and turn the ghosts off when they are in the way.

Jon: "there should also be lock icons for animations and for objects since i keep accidently
selecting the wrong ones. also i should be able to show or hide the ghosts"

Same idea the scene placer already uses (its LOCK button and padlock). A locked thing is drawn
exactly as before, and simply cannot be picked, dragged or deleted by a click - the list is still
the way to get at it, so nothing becomes unreachable.

The ghosts are a separate switch: they show WHERE something may happen, which is invaluable while
placing and pure clutter once it is placed.
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


# ---------------------------------------------------------------- locking
rep("""function whatIsUnder(q){
  const list=[];""",
    """// A locked thing is still drawn and still works; it just cannot be caught by a click.
function whatIsUnder(q){
  const list=[];""",
    "note on locking")

rep("""    if(!ruleNeeds(a)) continue;""",
    """    if(!ruleNeeds(a) || a.locked) continue;""",
    "locked animations ignore clicks")

rep("""  for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i], {w,h}=sizeOf(o);
    if(q.x>=o.x-w/2&&q.x<=o.x+w/2&&q.y>=o.y-h&&q.y<=o.y)
      list.push({kind:'object', i, o, label:nice(o.object)});
  }""",
    """  for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i]; if(o.locked) continue;
    const {w,h}=sizeOf(o);
    if(q.x>=o.x-w/2&&q.x<=o.x+w/2&&q.y>=o.y-h&&q.y<=o.y)
      list.push({kind:'object', i, o, label:nice(o.object)});
  }""",
    "locked objects ignore clicks")

rep("""const pickObj=q=>{ for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i], {w,h}=sizeOf(o);""",
    """const pickObj=q=>{ for(let i=scene.objects.length-1;i>=0;i--){
    const o=scene.objects[i]; if(o.locked) continue;
    const {w,h}=sizeOf(o);""",
    "and the older picker too")

# a locked thing cannot be deleted or nudged by the keyboard either
rep("""  else if((e.key==='Delete'||e.key==='Backspace')&&osel>=0){""",
    """  else if((e.key==='Delete'||e.key==='Backspace')&&osel>=0&&!scene.objects[osel].locked){""",
    "locked objects are not deleted by the key")

rep("""  if(nud && osel>=0){ const o=scene.objects[osel];""",
    """  if(nud && osel>=0 && !scene.objects[osel].locked){ const o=scene.objects[osel];""",
    "nor nudged")

rep("""  if(nud && osel<0 && anims[asel]){ const a=anims[asel];""",
    """  if(nud && osel<0 && anims[asel] && !anims[asel].locked){ const a=anims[asel];""",
    "and nor are locked animations")

# ---------------------------------------------------------------- the padlocks
rep("""    ? scene.objects.map((o,i)=>`<div class="obj" data-o="${i}" style="cursor:pointer;${
        i===osel?'color:var(--accent);font-weight:600':''}">${nice(o.object)}""",
    """    ? scene.objects.map((o,i)=>`<div class="obj" data-o="${i}" style="cursor:pointer;${
        i===osel?'color:var(--accent);font-weight:600':''}${o.locked?';opacity:.62':''}"
        ><button class="mini" data-lock="${i}" title="${o.locked?'locked - click to unlock'
        :'lock it so clicks pass over it'}" style="padding:0 4px;margin-right:5px"
        >${o.locked?'\\u{1F512}':'\\u{1F513}'}</button>${nice(o.object)}""",
    "a padlock on every object")

rep("""  $('objs').querySelectorAll('[data-o]').forEach(e=>e.onclick=()=>{
    osel=+e.dataset.o; zsel=-1; syncObj(); draw() });""",
    """  $('objs').querySelectorAll('[data-lock]').forEach(b=>b.onclick=e=>{
    e.stopPropagation();
    const o=scene.objects[+b.dataset.lock]; o.locked=!o.locked;
    if(o.locked && osel===+b.dataset.lock){ osel=-1; syncObj() }
    draw() });
  $('objs').querySelectorAll('[data-o]').forEach(e=>e.onclick=()=>{
    osel=+e.dataset.o; zsel=-1; syncObj(); draw() });""",
    "wire the object padlocks")

rep("""      <button class="mini" data-on="${i}" style="${a.on?
        'background:var(--accent);color:#fff;border-color:var(--accent)':'opacity:.55'}"
        >${a.on?'on':'off'}</button>""",
    """      <button class="mini" data-on="${i}" style="${a.on?
        'background:var(--accent);color:#fff;border-color:var(--accent)':'opacity:.55'}"
        >${a.on?'on':'off'}</button>
      <button class="mini" data-alock="${i}" title="${a.locked?'locked - click to unlock'
        :'lock it so clicks pass over it'}" style="padding:0 4px"
        >${a.locked?'\\u{1F512}':'\\u{1F513}'}</button>""",
    "a padlock on every animation")

rep("""  $('anims').querySelectorAll('[data-on]').forEach(b=>b.onclick=()=>{""",
    """  $('anims').querySelectorAll('[data-alock]').forEach(b=>b.onclick=e=>{
    e.stopPropagation();
    const a=anims[+b.dataset.alock]; a.locked=!a.locked; draw() });
  $('anims').querySelectorAll('[data-on]').forEach(b=>b.onclick=()=>{""",
    "wire the animation padlocks")

# ---------------------------------------------------------------- ghosts on and off
rep("""      <button id="preview">Act it out</button>""",
    """      <button id="preview">Act it out</button>
      <button id="ghosts">Ghosts: on</button>""",
    "a ghosts switch")

rep("""let osel=-1, drag=null;""",
    """let osel=-1, drag=null, showGhosts=true;""",
    "state for it")

rep("""$('preview').onclick=async()=>{""",
    """$('ghosts').onclick=()=>{ showGhosts=!showGhosts;
  $('ghosts').textContent='Ghosts: '+(showGhosts?'on':'off');
  $('ghosts').classList.toggle('pri',!showGhosts); draw() };
$('preview').onclick=async()=>{""",
    "wire it")

rep("""  const a=anims[asel];
  if(a && a.on && !simOn){""",
    """  const a=anims[asel];
  if(a && a.on && !simOn && showGhosts){""",
    "and honour it")

# they should not be clickable while hidden either
rep("""  if(!simOn) for(let i=anims.length-1;i>=0;i--){""",
    """  if(!simOn && showGhosts) for(let i=anims.length-1;i>=0;i--){""",
    "a hidden ghost cannot be clicked")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
