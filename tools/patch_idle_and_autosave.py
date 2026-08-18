"""His idle instead of a stock one, and stop the autosave quietly discarding half of each animation.

TWO THINGS.

1. The autosave wrote {name, on, rule, clip, fps, character, order, layers} and nothing else, so a
   refresh silently dropped stage (the authored SIZE and depth), place (where he sits on the
   thing), depth and facing. Everything came back at 100% sitting on the object's own anchor
   point. Losing work on reload is worse than not saving at all, because it looks like the tool
   forgot rather than broke.

2. "can he do idle instead of the turn in circles?" - yes, and it should be HIS idle, with its
   own props, breathing and size, not the stock Adult_Idle clip. Any animation can be marked as
   the one he does while he is between things; an `anywhere` animation is offered as that by
   default, since that is what "anywhere he walks" already means.
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


# ---------------------------------------------------------------- keep the whole animation
rep("""    anims: anims.map(a=>({name:a.name, on:a.on, rule:a.rule, clip:a.clip, fps:a.fps,
                          character:a.character, order:a.order, layers:a.layers})),""",
    """    // EVERYTHING about each animation. The first version of this listed seven fields and
    // quietly dropped stage/place/depth/facing, so a refresh reset every size to 100% and put
    // him back on each object's bare anchor point.
    anims: anims.map(a=>({name:a.name, on:a.on, rule:a.rule, clip:a.clip, fps:a.fps,
                          character:a.character, order:a.order, layers:a.layers,
                          stage:a.stage, place:a.place, depth:a.depth, facing:a.facing,
                          isIdle:a.isIdle})),""",
    "the autosave keeps the whole animation")

rep("""    const made=addAnim(a.name, a);
    made.on = a.on!==false;""",
    """    const made=addAnim(a.name, a);
    made.on = a.on!==false;
    // addAnim re-derives these from a project file; a restore already has the answers.
    if(a.stage)  made.stage=a.stage;
    if(a.place)  made.place=a.place;
    if(a.depth)  made.depth=a.depth;
    if(a.facing) made.facing=a.facing;
    if(a.isIdle) made.isIdle=true;""",
    "and puts it back")

# ---------------------------------------------------------------- his idle, not a stock one
rep("""          <div class="row" style="margin-top:6px">
            <button class="mini" id="apFront">in front of it</button>
            <button class="mini" id="apBehind">behind it</button>
          </div>""",
    """          <div class="row" style="margin-top:6px">
            <button class="mini" id="apFront">in front of it</button>
            <button class="mini" id="apBehind">behind it</button>
            <button class="mini" id="apIdle" title="what he does between things, instead of standing in the stock idle pose">use as his idle</button>
          </div>""",
    "a way to say which one is the idle")

rep("""$('facing').onchange=e=>{ const a=anims[asel]; if(!a)return;""",
    """$('apIdle').onclick=()=>{ const a=anims[asel]; if(!a)return;
  const now=!a.isIdle;
  anims.forEach(x=>x.isIdle=false);       // only one thing can be what he does in between
  a.isIdle=now; syncRule(); draw() };
$('facing').onchange=e=>{ const a=anims[asel]; if(!a)return;""",
    "wire it")

rep("""  $('apFront').classList.toggle('pri', a.depth==='front');""",
    """  $('apIdle').classList.toggle('pri', !!idleAnimFor(a));
  $('apIdle').textContent = idleAnimFor(a) ? 'his idle \\u2713' : 'use as his idle';
  $('apFront').classList.toggle('pri', a.depth==='front');""",
    "show which one it is")

# the rest beat plays it, in full
rep("""const idleClip=a=>{ const baby=phaseOf(a.clip)==='baby';
  return (baby?['Baby_Idle','Baby_Bored','Baby_Sit']:['Adult_Idle','Adult_Bored'])
    .find(k=>SD.clips[k]) || null };""",
    """// The animation he does between things: whichever is marked, or - failing that - the single
// animation that may happen anywhere, because that is already what "anywhere he walks" means.
function idleAnim(){
  const marked=anims.find(x=>x.isIdle && x.on);
  if(marked) return marked;
  const any=anims.filter(x=>x.on && !ruleNeeds(x));
  return any.length===1 ? any[0] : null;
}
const idleAnimFor=a=>(a && idleAnim()===a) ? a : null;
// The stock fallback, only when the scene has nothing of its own to idle in.
const idleClip=a=>{ const baby=phaseOf(a.clip)==='baby';
  return (baby?['Baby_Idle','Baby_Bored','Baby_Sit']:['Adult_Idle','Adult_Bored'])
    .find(k=>SD.clips[k]) || null };""",
    "which animation is the idle")

rep("""  } else if(sim.phase==='rest'){
    const c=idleClip(sim.a);""",
    """  } else if(sim.phase==='rest'){
    // His own idle if he has one - with its props, its breathing and its size - rather than a
    // stock pose. This is what "can he do idle instead of the turn in circles" asked for.
    const mine=idleAnim();
    if(mine){ drawChar(mine, Math.floor(sim.t*(mine.fps||7)), sim.x, sim.y, 1, false); return }
    const c=idleClip(sim.a);""",
    "he idles in his own animation")

# ...and the same while travelling, when the travel clip is missing
rep("""  if(sim.phase==='walk'){
    const c=sim.walkClip;""",
    """  if(sim.phase==='walk'){
    const c=sim.walkClip;
    if(!c){ const mine=idleAnim();
      if(mine){ drawChar(mine, Math.floor(sim.t*(mine.fps||7)), sim.x, sim.y, 1, false); return } }""",
    "and while travelling with no walk art")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
