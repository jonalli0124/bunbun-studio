"""One button that exports EVERYTHING: the scene JSON, every animation as an editor-openable
file, and the art the scene actually shows.

Jon: "need an export button that exports the full package, json, animations, visible art
essentially everything". One ZIP, store-only with real CRC32s (the same writer the editor
ships), named after the scene + stamp:

    <scene>_<stamp>/scene.json                     the device export, as Export writes it
    <scene>_<stamp>/animations/anim_<name>.json    each ON animation, editor project format
    <scene>_<stamp>/art/items/<object>.png         each visible object's PNG
    <scene>_<stamp>/art/room/<room>.png            the room itself
    <scene>_<stamp>/README.txt                     what everything is and where it goes
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


rep("""      <button id="exp" class="pri">Export for the device</button>""",
    """      <button id="exp" class="pri">Export for the device</button>
      <button id="expAll" title="one zip: the scene json, every animation as an editor file, and the art it shows">Export everything</button>""",
    "the button")

zipjs = (
    "// ---- Export everything: one zip of the whole scene ----\n"
    "// store-only zip with real CRC32s - the same approach the editor's bundle export uses\n"
    "const CRCT=(()=>{const t=new Uint32Array(256);for(let n=0;n<256;n++){let c=n;\n"
    "  for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[n]=c}return t})();\n"
    "const crc32=b=>{let c=0xFFFFFFFF;for(let i=0;i<b.length;i++)c=CRCT[(c^b[i])&255]^(c>>>8);\n"
    "  return (c^0xFFFFFFFF)>>>0};\n"
    "function zipStore(files){\n"
    "  const enc=new TextEncoder(); const parts=[]; const cd=[]; let off=0;\n"
    "  const u16=v=>[v&255,(v>>8)&255], u32=v=>[v&255,(v>>8)&255,(v>>16)&255,(v>>24)&255];\n"
    "  for(const [name,data] of files){\n"
    "    const nm=enc.encode(name), body=data instanceof Uint8Array?data:enc.encode(data);\n"
    "    const crc=crc32(body);\n"
    "    const head=new Uint8Array([0x50,0x4B,3,4,20,0,0,0,0,0,0,0,0,0,\n"
    "      ...u32(crc),...u32(body.length),...u32(body.length),...u16(nm.length),0,0]);\n"
    "    parts.push(head,nm,body);\n"
    "    cd.push({nm,crc,len:body.length,off});\n"
    "    off+=head.length+nm.length+body.length;\n"
    "  }\n"
    "  const cdParts=[]; let cdLen=0;\n"
    "  for(const e of cd){\n"
    "    const c=new Uint8Array([0x50,0x4B,1,2,20,0,20,0,0,0,0,0,0,0,0,0,\n"
    "      ...u32(e.crc),...u32(e.len),...u32(e.len),...u16(e.nm.length),\n"
    "      0,0,0,0,0,0,0,0,0,0,...u32(e.off)]);\n"
    "    cdParts.push(c,e.nm); cdLen+=c.length+e.nm.length;\n"
    "  }\n"
    "  const end=new Uint8Array([0x50,0x4B,5,6,0,0,0,0,...u16(cd.length),...u16(cd.length),\n"
    "    ...u32(cdLen),...u32(off),0,0]);\n"
    "  return new Blob([...parts,...cdParts,end],{type:'application/zip'});\n"
    "}\n"
    "async function pngBytes(url){ const r=await fetch(url); return new Uint8Array(await r.arrayBuffer()) }\n"
    "$('expAll').onclick=async()=>{\n"
    "  say('packing everything\\u2026');\n"
    "  const d=new Date(), z2=n=>String(n).padStart(2,'0');\n"
    "  const stamp=d.getFullYear()+z2(d.getMonth()+1)+z2(d.getDate())+'-'+z2(d.getHours())+z2(d.getMinutes());\n"
    "  const given=(($('sceneName')&&$('sceneName').value)||'').trim()||scene.room||'scene';\n"
    "  const stem=given.replace(/[" + BS + "/:*?\"<>|]+/g,'-').replace(/" + BS + "s+/g,' ').trim().slice(0,50);\n"
    "  const root=stem+'_'+stamp;\n"
    "  const files=[];\n"
    "  files.push([root+'/scene.json', JSON.stringify(forDevice(),null,1)]);\n"
    "  for(const a of anims){ if(!a.on) continue;\n"
    "    const proj={format:'bunbun-attach-project', v:1,\n"
    "      kind:'authored settings - re-open with import',\n"
    "      savedAt:new Date().toISOString(), savedAtLocal:new Date().toLocaleString(),\n"
    "      clip:a.clip, fps:a.fps, character:{...a.character},\n"
    "      order:a.order.map(o=>({...o})),\n"
    "      layers:['CHARACTER', ...a.layers.map(l=>({...l}))],\n"
    "      stage:a.stage?{...a.stage}:undefined,\n"
    "      rule:a.rule, place:a.place, facing:a.facing, depth:a.depth};\n"
    "    const an=a.name.replace(/[" + BS + "/:*?\"<>|]+/g,'-').slice(0,60);\n"
    "    files.push([root+'/animations/anim_'+an+'.json', JSON.stringify(proj,null,1)]);\n"
    "  }\n"
    "  const seen=new Set();\n"
    "  for(const o of scene.objects){ if(o.hidden||seen.has(o.object)) continue; seen.add(o.object);\n"
    "    const dprop=SD.props[o.object]; if(!dprop) continue;\n"
    "    try{ files.push([root+'/art/items/'+o.object+'.png', await pngBytes(dprop.img)]) }\n"
    "    catch(e){}\n"
    "  }\n"
    "  try{ files.push([root+'/art/room/'+scene.room+'.png', await pngBytes(SD.rooms[scene.room])]) }catch(e){}\n"
    "  files.push([root+'/README.txt',\n"
    "    'Scene: '+given+'  ('+new Date().toLocaleString()+')" + BS + "n'+\n"
    "    'scene.json      -> push with scene_from_assembler.py, or Import a scene here" + BS + "n'+\n"
    "    'animations/*    -> open in Animation Creation (load animation), or Add animations here" + BS + "n'+\n"
    "    'art/            -> the images this scene shows" + BS + "n']);\n"
    "  const blob=zipStore(files);\n"
    "  const el=document.createElement('a');\n"
    "  el.href=URL.createObjectURL(blob); el.download=root+'.zip'; el.click();\n"
    "  setTimeout(()=>URL.revokeObjectURL(el.href),5000);\n"
    "  say('packed '+files.length+' files into '+root+'.zip');\n"
    "};\n"
)

rep("""$('fsBtn').onclick=()=>{""",
    zipjs + """$('fsBtn').onclick=()=>{""",
    "the packer")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
