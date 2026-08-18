"""Panels only rebuild when their content changes - and Game view sits beside the editor.

Jon: "i have just animations running purple - and still can't click the animations buttons".
The same disease as the original rain bug: draw() runs ~18x a second while anything animates,
and it rebuilt the animation list, the make-him row, the object list and the zone list EVERY
FRAME - so any button in them was destroyed between mouse-down and mouse-up. Every panel now
renders to a string first and only touches the DOM when the string differs.

And Game view becomes side-by-side: the portrait panel appears NEXT TO the editor view rather
than replacing it, so you can direct him in one and watch the shipped screen in the other.
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


# ---------------------------------------------------------------- the guard
rep("""function drawAnims(){
  ensureAcodes();""",
    """// Rebuild a panel ONLY when its content string changes. draw() runs every frame while
// anything animates; rebuilding the DOM that often destroys the very button being clicked.
const _sig={};
function setIf(id, html){
  if(_sig[id]===html) return false;
  _sig[id]=html; $(id).innerHTML=html; return true;
}
function drawAnims(){
  ensureAcodes();""",
    "the guard")

rep("""  $('forceRow').innerHTML = anims.filter(a=>a.on).length""",
    """  const forceHtml = anims.filter(a=>a.on).length""",
    "force row renders to a string")

rep("""    : '';
  $('forceRow').querySelectorAll('[data-force]').forEach(b=>b.onclick=async()=>{""",
    """    : '';
  if(setIf('forceRow', forceHtml))
  $('forceRow').querySelectorAll('[data-force]').forEach(b=>b.onclick=async()=>{""",
    "and binds only on change")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
