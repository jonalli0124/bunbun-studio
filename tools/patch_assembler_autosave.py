"""Keep the assembler's work across a refresh, the way the editor already does.

Without this a reload throws away the room, everything placed in it, the ground you drew, the
animations you brought in and where the light was - so the tool punishes you for refreshing, and
you end up running an hours-old build because reloading costs too much.

Carried over verbatim from the editor's three hard-won rules:
  1. READ storage BEFORE the first render, or boot's own draw() overwrites the save.
  2. A `booting` guard, so nothing saves until the restore has had its turn.
  3. If the restore FAILS, never save over it - the work is still in there and a fresh autosave
     would destroy it.
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


rep("""$('room').innerHTML=Object.keys(SD.rooms).map(k=>`<option>${k}</option>`).join('');
// The farmhouse is the empty one, so it is where a scene of your own starts.
if(SD.rooms.farmhouse) $('room').value='farmhouse';
scene.room=$('room').value;
load(SD.rooms[scene.room],'room|'+scene.room).then(draw);
say('import a scene from the editor, then add the animations you exported');""",
    """$('room').innerHTML=Object.keys(SD.rooms).map(k=>`<option>${k}</option>`).join('');
// The farmhouse is the empty one, so it is where a scene of your own starts.
if(SD.rooms.farmhouse) $('room').value='farmhouse';
scene.room=$('room').value;

// ---------------------------------------------------------------- keep it across a refresh
const SKEY='bunbun-scene-tool-autosave';
let booting=true;
// Everything the room is: what is in it, what may be done there, where the ground is, which
// animations came in, and how it is lit. The art itself is not saved - it is rebuilt from the
// pack on load, so this stays small.
function session(){
  return {v:1, savedAt:new Date().toISOString(),
    scene, passiveScale, travelScale,
    anims: anims.map(a=>({name:a.name, on:a.on, rule:a.rule, clip:a.clip, fps:a.fps,
                          character:a.character, order:a.order, layers:a.layers})),
    light:{todMin, todOn, lampMode, lightScale},
    weather:{raining, clouding, cloudN, cloudStyle}};
}
let saveTimer=0;
function autosave(){ if(booting||saveTimer) return;
  // draw() fires ~18x a second while anything animates; writing the whole session that often is
  // the same thrash that made the editor unclickable under rain. One save a second is plenty.
  saveTimer=setTimeout(()=>{ saveTimer=0;
    try{ localStorage.setItem(SKEY,JSON.stringify(session())) }catch(e){} }, 1000) }

async function restore(raw){
  const p=JSON.parse(raw);
  if(p.scene){
    scene=p.scene;
    scene.objects=scene.objects||[]; scene.zones=scene.zones||[];
    if(SD.rooms[scene.room]) $('room').value=scene.room; else scene.room=$('room').value;
  }
  if(typeof p.passiveScale==='number'){ passiveScale=p.passiveScale;
    $('ps').value=Math.round(passiveScale*100); $('psl').textContent=Math.round(passiveScale*100)+'%' }
  if(typeof p.travelScale==='number'){ travelScale=p.travelScale;
    $('ts').value=Math.round(travelScale*100); $('tsl').textContent=Math.round(travelScale*100)+'%' }
  if(p.light){ const L=p.light;
    if(typeof L.todMin==='number'){ todMin=L.todMin; $('tod').value=todMin; $('todl').textContent=hhmm(todMin) }
    if(typeof L.todOn==='boolean'){ todOn=L.todOn;
      $('lightOff').textContent='lighting: '+(todOn?'on':'off');
      $('lightOff').classList.toggle('pri',!todOn) }
    if(L.lampMode){ lampMode=L.lampMode; $('lampM').textContent='lamps: '+lampMode }
    if(typeof L.lightScale==='number'){ lightScale=L.lightScale;
      $('lsz').value=Math.round(lightScale*100); $('lszl').textContent=Math.round(lightScale*100)+'%' } }
  if(p.weather){ const W=p.weather;
    raining=!!W.raining; clouding=!!W.clouding;
    $('rn').classList.toggle('pri',raining); $('cl').classList.toggle('pri',clouding);
    if(typeof W.cloudN==='number'){ cloudN=W.cloudN; $('cn').value=cloudN; $('cnl').textContent=cloudN }
    if(W.cloudStyle!=null){ cloudStyle=W.cloudStyle;
      $('cst').value=cloudStyle } }

  // the art for everything restored
  await load(SD.rooms[scene.room],'room|'+scene.room);
  await Promise.all(scene.objects.filter(o=>SD.props[o.object])
    .map(o=>load(SD.props[o.object].img,'prop|'+o.object)));
  anims.length=0;
  for(const a of (p.anims||[])){
    if(!a.clip || !SD.clips[a.clip]) continue;      // art that is no longer in the pack
    const made=addAnim(a.name, a);
    made.on = a.on!==false;
    await loadArtFor(made);
  }
  rebuildClouds(); weatherInit(scene.room); litCache.key='';
  return {objects:scene.objects.length, anims:anims.length,
          zones:scene.zones.length, dropped:(p.anims||[]).length-anims.length};
}

(async()=>{
  // READ FIRST. draw() autosaves, and boot's own draw would otherwise wipe the save before
  // this line ever looked at it - the exact bug the editor hit.
  let raw=null, failed=false;
  try{ raw=localStorage.getItem(SKEY) }catch(e){}
  await load(SD.rooms[scene.room],'room|'+scene.room);
  if(raw){
    try{
      const r=await restore(raw);
      const bits=[scene.room];
      if(r.objects) bits.push(r.objects+' object'+(r.objects===1?'':'s'));
      if(r.zones)   bits.push(r.zones+' shape'+(r.zones===1?'':'s')+' on the ground');
      if(r.anims)   bits.push(r.anims+' animation'+(r.anims===1?'':'s'));
      say('picked up where you left off \\u2014 '+bits.join(', ')+
          (r.dropped>0?`; ${r.dropped} animation${r.dropped===1?'':'s'} used art no longer in the pack`:''));
    }catch(e){ console.warn('restore failed',e); failed=true;
      say('could not restore your last scene \\u2014 it is still saved, nothing was lost',true) }
  } else say('import a scene from the editor, then add the animations you exported');
  booting=false;
  // A failed restore stays on disk. Saving now would overwrite the thing that failed to load.
  if(!failed) autosave();
  draw();
})();""",
    "restore before the first render")

# every draw is a good moment to save - it is the one thing every change already calls
rep("""  drawAnims(); drawObjs(); $('out').textContent=JSON.stringify(forDevice(),null,1);""",
    """  drawAnims(); drawObjs(); $('out').textContent=JSON.stringify(forDevice(),null,1);
  autosave();""",
    "save on every change")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
