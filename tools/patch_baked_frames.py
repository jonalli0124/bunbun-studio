"""The bundle ships COMPOSED frames - pet with props already attached - and a two-answer
RENDER.md. The raw clips, separate props and anchors.json go; baking makes them unnecessary.

Jon: "Just include the COMPOSED frames per animation ... Then I don't need the anchor
derivation at all. What I still need: 1. What place.dx/dy is measured from, per rule type.
2. How passiveScale, travelScale and stage.scale combine."

Baking uses the same maths drawChar uses on the stage - anchor + offset, deterministic jiggle
and pulse by step index, per-entry character scale about its pivot - at 2x sprite resolution
(192x192), one PNG per order step, under art/baked/<animation>/NN.png. Breathing is runtime
motion and is NOT baked; its numbers ride in the animation JSONs.
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


# ---------------------------------------------------------------- baked frames replace raw art
rep("""  // THE CHARACTER'S OWN FRAMES - exactly the ones each live animation's order plays - and every
  // prop those animations carry. Without these a bundle could describe motion but not draw it.
  const clipFrames=new Map();     // clip -> Set of frame indices used
  const propSeen=new Set();
  for(const a of anims){ if(!a.on) continue;
    if(!clipFrames.has(a.clip)) clipFrames.set(a.clip, new Set());
    const set=clipFrames.get(a.clip);
    for(const o of a.order) set.add(o.i);
    // a rotation clip HOLDS its facing view at runtime - which may not be in the order at
    // all, so it goes in explicitly or the bundle ships the wrong compass points
    if(isRot(a.clip)) set.add(rotFrame(a.clip, a.facing||faceOnOf(a.clip)));
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
  files.push([root+'/art/anchors.json', JSON.stringify(anchorDump,null,1)]);""",
    """  // COMPOSED FRAMES: pet with props already attached, one PNG per order step, using the same
  // maths the stage draws with. No raw clips, no separate props, no anchor derivation needed
  // on the far side - the composition is done here, at 2x sprite resolution.
  for(const a of anims){ if(!a.on) continue;
    await loadArtFor(a);
    const dirName=a.name.replace(/[\\/:*?"<>|]+/g,'-').slice(0,50);
    for(let s2=0;s2<a.order.length;s2++){
      const bytes=await bakeStep(a, s2);
      if(bytes) files.push([root+'/art/baked/'+dirName+'/'+String(s2).padStart(2,'0')+'.png', bytes]);
    }
  }""",
    "baked frames replace the raw pieces")

# ---------------------------------------------------------------- the baker
rep("""async function pngBytes(url){""",
    """// One order step, composed exactly as drawChar composes it - per-entry character scale about
// its pivot, props at anchor+offset with deterministic jiggle/pulse by step index. Breathing
// is runtime motion and deliberately NOT baked.
async function bakeStep(a, s2){
  const K=2, cvB=document.createElement('canvas'); cvB.width=96*K; cvB.height=96*K;
  const g=cvB.getContext('2d'); g.imageSmoothingEnabled=false;
  const ent=a.order[s2]||{i:0,cs:100};
  const fi=isRot(a.clip) ? rotFrame(a.clip, a.facing||faceOnOf(a.clip)) : ent.i;
  const f=SD.clips[a.clip] && SD.clips[a.clip].frames[fi]; if(!f) return null;
  const im=imgs['clip|'+a.clip+'|'+fi] || imgs['clip|'+a.clip+'|0']; if(!im) return null;
  const k=(ent.cs==null?100:ent.cs)/100;
  const bb=f.bbox||[0,0,96,96];
  const cx2=(bb[0]+bb[2])/2, cy2=(a.character.pivot==='middle')?(bb[1]+bb[3])/2:bb[3];
  g.save();
  if(Math.abs(k-1)>1e-6){ g.translate(cx2*K,cy2*K); g.scale(k,k); g.translate(-cx2*K,-cy2*K) }
  g.drawImage(im,0,0,96*K,96*K);
  for(const L of a.layers){
    const d=SD.props[L.prop], pi=imgs['prop|'+L.prop];
    if(!d||!pi||L.on===false) continue;
    let pt = L.anchor==='head' ? f.head
           : L.anchor==='feet' ? (f.bbox?[(f.bbox[0]+f.bbox[2])/2,f.bbox[3]]:null)
           : f[L.anchor];
    if(!pt) continue;
    const off=L.off||{x:0,y:0};
    const jx=L.jig?Math.sin(s2*2.4)*L.jig:0, jy=L.jig?Math.cos(s2*3.1)*L.jig*0.6:0;
    const puff=L.pulse?1+(L.pulse/100)*Math.sin(2*Math.PI*s2/Math.max(2,L.pper||8)):1;
    const w=d.w*(L.scale||1)*puff*K, h=d.h*(L.scale||1)*puff*K;
    g.save(); g.globalAlpha=(L.opac==null?100:L.opac)/100;
    g.translate((pt[0]+off.x+jx)*K,(pt[1]+off.y+jy)*K);
    if(L.rot) g.rotate(L.rot*Math.PI/180);
    if(L.flip) g.scale(-1,1);
    g.drawImage(pi,-w/2,-h/2,w,h);
    g.restore();
  }
  g.restore();
  const blob=await new Promise(r=>cvB.toBlob(r,'image/png'));
  return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
}
async function pngBytes(url){""",
    "the baker")

# ---------------------------------------------------------------- RENDER.md: two answers
rep("""function renderDoc(){ return [""",
    """function renderDoc(){ return [
'# Rendering this bundle',
'',
'art/baked/<animation>/NN.png are COMPOSED frames - pet with props attached - one per order',
'step, 192x192 (2x the 96px sprite; centre-feet at (96,180)). Play them at the animation\\'s',
'fps, each held `character.hold` frames. Breathing is not baked: scale the whole frame by',
'1 + (breathe/100)*sin(2*pi*step/period), pinned at the feet.',
'',
'## 1. place.dx/dy - measured from what?',
'Depends on the rule:',
'- needs / onItems: the AFFORDING OBJECT\\'s (x,y) - its base line, bottom-centre of its',
'  sprite. settle = (obj.x+dx, obj.y+dy), clamped to max(22,(w+h)/2*0.9)px of that object\\'s',
'  scaled sprite. bathe\\'s {dx:-2,dy:-20} means 2px left, 20px up from the tub\\'s base.',
'- inArea: the area\\'s standing point (centroid if standable, else the nearest standable',
'  point inside it) + (dx,dy).',
'- anywhere: NOWHERE - the offset is a preview aid in the tool and is ignored at runtime.',
'  Idle\\'s {dx:134,dy:8,"placed by hand"} does not move where he idles; "anywhere" means',
'  wherever he stops. The `from` string is provenance for humans, never a different origin.',
'',
'## 2. how the three scales combine',
'They apply to DIFFERENT things and never stack:',
'- character in an AUTHORED animation: phaseScale * stage.scale/100',
'  (phaseScale = 0.78*1.45 baby, 1.00*1.45 adult; so stage.scale 35 -> 1.45*0.35 = 0.5075x',
'  applied to the 96px frame).',
'- character TRAVELLING between things (walk/idle clips the scene chose): phaseScale *',
'  travelScale. travelScale is a SIZE, never a speed - walking is a constant 42px/s.',
'- every OBJECT: object.scale * passiveScale (the exported scene.json already folds passive',
'  into each object\\'s `scale`, keeping the original in `authoredScale`).',
''].join('\\n'); }
function renderDocOld(){ return [""",
    "two answers, baked-frame preamble")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
