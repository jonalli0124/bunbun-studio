"""The library holds ANIMATIONS, not whole scenes.

Jon: "i have this sit / breathe created and i want it to only happen on the chair, but let's
say i do juggle... i want him to be able to juggle anywhere he walks."

So an animation is what the character DOES - the clip, its frame order and holds, the objects
attached to it, its breathing - plus ONE rule saying where it is allowed. A scene is where and
among what. They are separate because one room hosts many animations, and one animation must
survive being dropped into a different room.

Two consequences fall straight out:
  - the rule lives on the ANIMATION, not the clip, so two animations built on Adult_Sit can
    have different rules;
  - loading an animation leaves the room, its objects, zones, lights and weather alone.
"""
import io, os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "attach_editor.html")
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


rep("const LIB='bunbun-attach-library';",
    """const LIB='bunbun-attach-library';
// what the character DOES - portable between rooms
function animCore(){
  const p=projectCore();
  return {clip:p.clip, fps:p.fps, character:p.character, order:p.order, layers:p.layers};
}
// applied ON TOP of whatever room is already staged, never replacing it
async function applyAnim(a){
  const keep=projectCore();
  await loadProject(Object.assign({}, keep, {
    clip:a.clip, fps:a.fps, character:a.character, order:a.order, layers:a.layers}));
}
let animRule={mode:'anywhere'};              // the rule for the animation being edited
const ruleText=r=>r.mode==='anywhere'?'anywhere he can walk':('only where something affords '+r.needs);""",
    "animCore / applyAnim")

rep("""$('libSave').onclick=()=>{
  const n=($('libName').value||'').trim().slice(0,40);
  if(!n) return flash('give it a name first',true);
  const all=libAll(); all[n]=projectCore();""",
    """$('libSave').onclick=()=>{
  const n=($('libName').value||'').trim().slice(0,40);
  if(!n) return flash('give it a name first',true);
  const all=libAll(); all[n]={kind:'animation', anim:animCore(), rule:{...animRule}};""",
    "keep stores an animation")

rep("""function libLoadName(n){
  const all=libAll(); if(!all[n]) return;
  loadProject(all[n]).then(()=>{ $('libName').value=n; flash('opened "'+n+'"') })
    .catch(e=>flash('could not open "'+n+'" \\u2014 '+e.message,true));
}""",
    """function libLoadName(n){
  const all=libAll(), rec=all[n]; if(!rec) return;
  // an entry saved before the split is a whole scene; a new one is just the animation
  const isAnim = rec && rec.kind==='animation';
  const p = isAnim ? applyAnim(rec.anim) : loadProject(rec);
  Promise.resolve(p).then(()=>{
    animRule = (isAnim && rec.rule) ? {...rec.rule} : {mode:'anywhere'};
    $('libName').value=n; libDraw(); render();
    flash(isAnim ? `opened "${n}" \\u2014 ${ruleText(animRule)}; your room is untouched`
                 : `opened "${n}" (an older whole-scene entry)`);
  }).catch(e=>flash('could not open "'+n+'" \\u2014 '+e.message,true));
}""",
    "loading keeps the room")

rep("""      <div class="hint" id="libInfo" style="margin-top:6px"></div>""",
    """      <div class="row" style="margin-top:6px;align-items:center">
        <span class="hint" style="flex:0 0 auto">this animation happens</span>
        <button class="mini" id="arAny">anywhere he walks</button>
        <select id="arNeeds" style="flex:0 0 130px;font-size:12px;padding:3px">
          <option value="sit">only on a sit spot</option>
          <option value="sleep">only on a sleep spot</option>
          <option value="surface">only on a surface</option>
          <option value="eat">only at an eat spot</option>
          <option value="play">only at a play spot</option>
        </select>
      </div>
      <div class="hint" id="libInfo" style="margin-top:6px"></div>""",
    "per-animation rule control")

rep("""$('libPrev').onclick=()=>libStep(-1);""",
    """$('arAny').onclick=()=>{ animRule={mode:'anywhere'}; libDraw(); render() };
$('arNeeds').onchange=e=>{ animRule={mode:'needs',needs:e.target.value}; libDraw(); render() };
$('libPrev').onclick=()=>libStep(-1);""",
    "rule wiring")

rep("""  const open=($('libName').value||'').trim();
  $('libInfo').textContent = names.length""",
    """  const open=($('libName').value||'').trim();
  $('arAny').classList.toggle('pri', animRule.mode==='anywhere');
  if(animRule.mode==='needs') $('arNeeds').value=animRule.needs;
  $('libInfo').textContent = names.length""",
    "rule readout")

# the export names the animation and its rule
rep("""           actionRules:Object.fromEntries(rulePoses().map(c=>{const r=ruleFor(c);""",
    """           animation:{name:($('libName').value||'').trim()||null,
                      clip:clip,
                      rule: animRule.mode==='anywhere' ? {anywhere:true} : {needs:animRule.needs},
                      where: ruleText(animRule)},
           actionRules:Object.fromEntries(rulePoses().map(c=>{const r=ruleFor(c);""",
    "animation + rule in the export")

rep("   zones:zones.map(z=>({...z})), actions:acts.map(a=>({...a})), actionRules:{...arules},",
    "   zones:zones.map(z=>({...z})), actions:acts.map(a=>({...a})), actionRules:{...arules},\n"
    "   animRule:{...animRule},",
    "save the rule with the project")
rep("    arules=(p.actionRules&&typeof p.actionRules==='object')?{...p.actionRules}:{};",
    "    arules=(p.actionRules&&typeof p.actionRules==='object')?{...p.actionRules}:{};\n"
    "    if(p.animRule&&p.animRule.mode) animRule={...p.animRule};",
    "load the rule")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
