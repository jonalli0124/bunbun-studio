"""Save the working scene as a file and open it again; the library bridge retires.

Jon: "we also need a save and import scene button and drop the from animation clip button since
that is now deprecated".

SAVE SCENE writes the whole working state - the same session the autosave and undo already use:
objects, shapes with their names and colours, every animation with its rule/place/facing/size,
the scene name, the scales. IMPORT recognises it and restores it exactly; the same button still
accepts a device export (the older, lossier shape) and says which kind it got.

"From Animation Creation" read the editor's localStorage library - retired along with the
library itself. Files are the way animations move now.
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


rep("""      <button id="impScene">Import a scene</button>""",
    """      <button id="saveScene" class="pri">&#128190; Save scene</button>
      <button id="impScene">&#128194; Open scene</button>""",
    "save + open, side by side")

rep("""      <button id="fromLib" title="for when Animation Creation is open beside this - importing the file you saved is the tidier way">From Animation Creation</button>""",
    """""",
    "the library bridge retires")

rep("""$('impScene').onclick=()=>$('fScene').click();""",
    """$('saveScene').onclick=()=>{
  const j=Object.assign({format:'bunbun-scene-project', v:1}, session());
  const d=new Date(), z=n=>String(n).padStart(2,'0');
  const stamp=d.getFullYear()+z(d.getMonth()+1)+z(d.getDate())+'-'+z(d.getHours())+z(d.getMinutes());
  const given=(($('sceneName')&&$('sceneName').value)||'').trim()||scene.room||'scene';
  const stem=given.replace(/[\\/:*?"<>|]+/g,'-').replace(/\\s+/g,' ').trim().slice(0,50);
  const el=document.createElement('a');
  el.href=URL.createObjectURL(new Blob([JSON.stringify(j,null,1)],{type:'application/json'}));
  el.download='scene_'+stem+'_'+stamp+'.json'; el.click();
  setTimeout(()=>URL.revokeObjectURL(el.href),5000);
  say(`saved "${given}" \\u2014 Open scene brings it back exactly as it is now`) };
$('impScene').onclick=()=>$('fScene').click();""",
    "save writes the whole working state")

rep("""$('fScene').onchange=async e=>{
  const f=e.target.files[0]; e.target.value=''; if(!f) return;
  let p; try{ p=JSON.parse(await f.text()) }catch(err){ return say('that file is not readable',true) }""",
    """$('fScene').onchange=async e=>{
  const f=e.target.files[0]; e.target.value=''; if(!f) return;
  let p; try{ p=JSON.parse(await f.text()) }catch(err){ return say('that file is not readable',true) }
  // a saved scene PROJECT restores exactly - it is the same shape the autosave and undo use
  if(p.format==='bunbun-scene-project'){
    booting=true;
    try{ await restore(JSON.stringify(p)) }
    catch(err){ booting=false; return say('could not open that scene \\u2014 '+err.message,true) }
    booting=false; histPush(JSON.stringify(session()));
    syncRule(); syncObj(); draw();
    return say(`opened "${p.sceneName||f.name}" \\u2014 everything as it was saved`);
  }""",
    "open recognises a project and restores it")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
