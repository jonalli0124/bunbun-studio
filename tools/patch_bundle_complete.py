"""The scene bundle becomes renderable outside the tools: character frames, attached props, and
a RENDER.md that answers the four semantic questions. Plus: the archive verifies itself before
it is handed over.

Jon (rendering a bundle externally) found:
  - no character clips and no attached props in art/ - only the room and furniture
  - the place/facing/anchor/scale semantics undocumented
  - and both zips he had failed on the central directory. The two-byte CD fix landed after his
    downloads, but "worth checking the writer finalises" is the right instinct - so the writer
    now PARSES ITS OWN OUTPUT with an independent walk of the central directory before handing
    the file over, and refuses to download a zip it cannot read back.
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


# ---------------------------------------------------------------- clips + props in the zip
rep("""  const seen=new Set();
  for(const o of scene.objects){ if(o.hidden||seen.has(o.object)) continue; seen.add(o.object);
    const dprop=SD.props[o.object]; if(!dprop) continue;
    try{ files.push([root+'/art/items/'+o.object+'.png', await pngBytes(dprop.img)]) }
    catch(e){}
  }""",
    """  const seen=new Set();
  for(const o of scene.objects){ if(o.hidden||seen.has(o.object)) continue; seen.add(o.object);
    const dprop=SD.props[o.object]; if(!dprop) continue;
    try{ files.push([root+'/art/items/'+o.object+'.png', await pngBytes(dprop.img)]) }
    catch(e){}
  }
  // THE CHARACTER'S OWN FRAMES - exactly the ones each live animation's order plays - and every
  // prop those animations carry. Without these a bundle could describe motion but not draw it.
  const clipFrames=new Map();     // clip -> Set of frame indices used
  const propSeen=new Set();
  for(const a of anims){ if(!a.on) continue;
    if(!clipFrames.has(a.clip)) clipFrames.set(a.clip, new Set());
    const set=clipFrames.get(a.clip);
    for(const o of a.order) set.add(o.i);
    for(const L of a.layers){ if(L && L.prop) propSeen.add(L.prop) }
  }
  for(const [clip,idx] of clipFrames){
    const c=SD.clips[clip]; if(!c) continue;
    for(const i of [...idx].sort((a2,b2)=>a2-b2)){
      const f=c.frames[i]; if(!f) continue;
      const nm=f.dir!=null ? f.dir : String(i).padStart(2,'0');
      try{ files.push([root+'/art/clips/'+clip+'/'+nm+'.png', await pngBytes(f.img)]) }catch(e){}
    }
  }
  for(const pn of propSeen){
    const dp=SD.props[pn]; if(!dp) continue;
    try{ files.push([root+'/art/props/'+pn+'.png', await pngBytes(dp.img)]) }catch(e){}
  }
  // the anchors those frames were measured with, so an external renderer can place the props
  const anchorDump={};
  for(const [clip] of clipFrames){ const c=SD.clips[clip]; if(!c) continue;
    anchorDump[clip]={rotations:!!c.rotations, faceOn:c.faceOn||null,
      frames:c.frames.map(f=>({name:f.name, dir:f.dir||null, bbox:f.bbox||null,
        head:f.head||null, L:f.L||null, R:f.R||null, mid:f.mid||null}))};
  }
  files.push([root+'/art/anchors.json', JSON.stringify(anchorDump,null,1)]);
  files.push([root+'/RENDER.md', renderDoc()]);""",
    "clips, props, anchors and the doc travel")

# ---------------------------------------------------------------- RENDER.md
rep("""async function pngBytes(url){""",
    """// The four questions an external renderer has to have answered, answered from the code that
// draws the preview - not from memory.
function renderDoc(){ return [
'# Rendering this bundle',
'',
'Room coordinates are 320x240; an object\\'s (x,y) is its BASE LINE (bottom-centre of the',
'sprite); z sorts back-to-front (feet y when z is null). The character sprite is 96x96 with',
'(48,90) as its middle/feet line.',
'',
'## 1. place.dx/dy - relative to what?',
'The rule type decides the ANCHOR the offset is added to:',
'- needs / onItems: the OBJECT\\'s (x,y). settle = (obj.x+dx, obj.y+dy), clamped to the',
'  object\\'s reach: max(22, (w+h)/2 * 0.9) px of its scaled sprite. He walks TO the settle',
'  point, not to the object.',
'- inArea: the area\\'s standing point - its centroid if he can stand there, else the nearest',
'  standable point found inside it. settle = that point + (dx,dy), clamp default 26px.',
'- anywhere: THE OFFSET IS PREVIEW-ONLY. It moves the ghost in the tool and is ignored at',
'  runtime - "anywhere" means he does it wherever he stops.',
'The `from` field is provenance for humans ("measured from...", "placed by hand") and never',
'changes the maths.',
'',
'## 2. facing',
'Seven clips are ROTATION SETS - 8 compass views, one held still (sit, sleep, idle, crawl,',
'onesie families). `facing` names the view to hold: east/north-east/north/north-west/west/',
'south-west/south/south-east. When facing is null, use the measured face-on view shipped in',
'anchors.json as `faceOn` (Adult_Sit: south-east; the others: south).',
'Sequence clips (Bathe, Eat, Angry, Walk_*, ...) have FRAMES, not views - facing is null and',
'meaningless there; play `order` in sequence, each entry held `character.hold` frames at',
'`fps`. Travel facing is a CLIP swap (Walk_East vs Walk_West), never a mirror.',
'',
'## 3. anchors (per frame, in anchors.json)',
'Derived from the pixels (tools/mkdata.py anchors()):',
'- bbox: the alpha>127 bounding box.',
'- head: (bbox centre x, bbox top y).',
'- feet: not stored - derive as (bbox centre x, bbox bottom y).',
'- L / R hands: within the top 80% of the bbox, find fur-coloured pixels',
'  (#be7330 / #804d36) strictly LEFT of the tunic\\'s leftmost purple pixel (#9151d3 /',
'  #5d229d) on that row -> L candidates; strictly right -> R. The anchor is the centroid of',
'  the LOWEST candidates (within 2px of the bottom-most) - the hand tip.',
'- mid: midpoint of L and R when both exist.',
'Attached props draw at anchor + layer.off, scaled by layer.scale x the character\\'s scale,',
'with layer.follow damping toward the anchor\\'s mean position.',
'',
'## 4. the three scales',
'- stage.scale (percent): the CHARACTER during his authored animations.',
'  charScale = phaseScale * stage.scale/100, where phaseScale = 0.78*1.45 (baby) or',
'  1.00*1.45 (adult).',
'- travelScale: a SIZE, not a speed - the character while walking/idling BETWEEN things',
'  (clips the scene chose, which nobody authored). charScale = phaseScale * travelScale.',
'  It never applies to authored animations, and walk speed is a constant 42 px/s regardless.',
'- passiveScale: SCENERY only. Every object draws at object.scale * passiveScale. It never',
'  touches the character. (The exported scene.json already folds it into each object\\'s',
'  `scale`, keeping the original in `authoredScale`.)',
'These never stack with each other: an authored animation uses stage.scale, travel uses',
'travelScale, objects use passiveScale.',
''].join('\\n'); }
async function pngBytes(url){""",
    "RENDER.md answers all four")

# ---------------------------------------------------------------- the writer proves the archive
rep("""  const blob=zipStore(files);
  const el=document.createElement('a');
  el.href=URL.createObjectURL(blob); el.download=root+'.zip'; el.click();""",
    """  const blob=zipStore(files);
  // TRUST NOTHING THAT HAS NOT BEEN READ BACK. An independent walk of the central directory:
  // find EOCD, check entry count, then verify every CD record points at a real local header
  // whose name matches. A writer bug now stops the download instead of shipping a broken file.
  {
    const b=new Uint8Array(await blob.arrayBuffer());
    let eocd=-1;
    for(let i=b.length-22;i>=0 && i>b.length-22-65558;i--)
      if(b[i]===0x50&&b[i+1]===0x4B&&b[i+2]===5&&b[i+3]===6){ eocd=i; break }
    const rd16=o2=>b[o2]|(b[o2+1]<<8), rd32=o2=>(b[o2]|(b[o2+1]<<8)|(b[o2+2]<<16)|(b[o2+3]<<24))>>>0;
    let okZip = eocd>=0;
    if(okZip){
      const n=rd16(eocd+10), cdOff=rd32(eocd+16);
      let p2=cdOff, count=0;
      const dec2=new TextDecoder();
      while(okZip && count<n){
        if(!(b[p2]===0x50&&b[p2+1]===0x4B&&b[p2+2]===1&&b[p2+3]===2)){ okZip=false; break }
        const nl=rd16(p2+28), el2=rd16(p2+30), cl=rd16(p2+32), lo=rd32(p2+42);
        const cdName=dec2.decode(b.slice(p2+46,p2+46+nl));
        if(!(b[lo]===0x50&&b[lo+1]===0x4B&&b[lo+2]===3&&b[lo+3]===4)){ okZip=false; break }
        const lnl=rd16(lo+26);
        if(dec2.decode(b.slice(lo+30,lo+30+lnl))!==cdName){ okZip=false; break }
        p2+=46+nl+el2+cl; count++;
      }
      if(count!==n) okZip=false;
    }
    if(!okZip){ say('the zip failed its own read-back check - NOT downloaded; this is a tool '+
      'bug worth reporting', true); return }
  }
  const el=document.createElement('a');
  el.href=URL.createObjectURL(blob); el.download=root+'.zip'; el.click();""",
    "the archive proves itself before download")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
