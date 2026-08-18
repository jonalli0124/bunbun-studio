"""Save one animation out of the assembler, in the attach editor's own project format.

Jon: "can we export the animations so i can re-import it back into the attach editor?" - which
closes the loop: tweak it there, save, and the replace button here swaps it back in. The
scene-side decisions (rule, place, facing, depth) ride along under their own keys; the editor
ignores them, and re-importing here reads them back.
"""
import io, os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "scene_tool.html")
s = io.open(SRC, encoding="utf-8").read()
done = []
BS = chr(92)


def rep(old, new, label):
    global s
    if old not in s:
        print("  SKIP:", label)
        return
    s = s.replace(old, new, 1)
    done.append(label)
    print("  ok:", label)


rep('      <button class="mini" data-ren="${i}" title="rename it">&#9998;</button>',
    '      <button class="mini" data-ren="${i}" title="rename it">&#9998;</button>\n'
    '      <button class="mini" data-exp="${i}" title="save this one as a file - it re-opens '
    'in the attach editor for tweaking">&#128190;</button>',
    "a save button on each row")

handler = (
    "  $('anims').querySelectorAll('[data-exp]').forEach(b=>b.onclick=ev=>{\n"
    "    ev.stopPropagation();\n"
    "    const a=anims[+b.dataset.exp];\n"
    "    // THE EDITOR'S OWN PROJECT SHAPE, so the round trip closes: tweak there, save, and\n"
    "    // the replace button here swaps it back in.\n"
    "    const proj={format:'bunbun-attach-project', v:1,\n"
    "      kind:'authored settings - re-open with import',\n"
    "      savedAt:new Date().toISOString(), savedAtLocal:new Date().toLocaleString(),\n"
    "      clip:a.clip, fps:a.fps,\n"
    "      character:{...a.character},\n"
    "      order:a.order.map(o=>({...o})),\n"
    "      layers:['CHARACTER', ...a.layers.map(l=>({...l}))],\n"
    "      stage:a.stage?{...a.stage}:undefined,\n"
    "      rule:a.rule, place:a.place, facing:a.facing, depth:a.depth};\n"
    "    const d=new Date(), z=n=>String(n).padStart(2,'0');\n"
    "    const stamp=d.getFullYear()+z(d.getMonth()+1)+z(d.getDate())+'-'+z(d.getHours())+z(d.getMinutes());\n"
    "    const stem=a.name.replace(/[" + BS + "/:*?\"<>|]+/g,'-').replace(/" + BS + "s+/g,'_').slice(0,50);\n"
    "    const el=document.createElement('a');\n"
    "    el.href=URL.createObjectURL(new Blob([JSON.stringify(proj,null,1)],{type:'application/json'}));\n"
    "    el.download='anim_'+stem+'_'+stamp+'.json'; el.click();\n"
    "    setTimeout(()=>URL.revokeObjectURL(el.href),5000);\n"
    "    say('saved \"'+a.name+'\" - open it in the attach editor with load animation') });\n"
)

rep("  $('anims').querySelectorAll('[data-rep]').forEach(b=>b.onclick=()=>{",
    handler + "  $('anims').querySelectorAll('[data-rep]').forEach(b=>b.onclick=()=>{",
    "it writes the editor project shape")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
