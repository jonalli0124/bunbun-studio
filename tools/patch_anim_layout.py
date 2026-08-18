"""Animation Creation gets the Scene Assembler's shape.

Jon: "we need to redesign the animation creation similar to how we did this UI because it is
hard to navigate". Everything lived in one narrow right-hand column; now it is the same three
logical columns as the assembler - the ANIMATION on the left (name, clip, backdrop, breathing,
save/load), the STAGE in the middle (centred, growing with the monitor, fullscreen button,
frame strip beneath it), the STACK & PROP EDITOR on the right - with draggable, remembered
dividers, and the baked/render output in a hover drawer against the right edge, exactly like
the assembler's JSON.

Done as a boot-time reshuffle of the existing markup rather than rewriting it: every id keeps
its element, every element keeps its handlers.
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


# ---------------------------------------------------------------- CSS: the assembler's grid
rep("""  main{display:flex;gap:18px;padding:18px;align-items:flex-start;flex-wrap:wrap}""",
    """  /* THREE COLUMNS, like the Scene Assembler: the animation | the stage | the props.
     Dividers drag, widths are remembered, and the stage grows with the monitor. */
  main{display:grid;gap:0 10px;padding:16px 34px 16px 16px;align-items:start;
       grid-template-columns:var(--wAnim,300px) 10px minmax(380px,1fr) 10px var(--wProps,352px);
       grid-template-areas:"anim g1 stage g2 props"}
  .col-anim{grid-area:anim}.col-stage{grid-area:stage}.col-props{grid-area:props}
  .col-anim,.col-stage,.col-props{display:flex;flex-direction:column;gap:14px;min-width:0}
  .gut{cursor:col-resize;align-self:stretch;border-radius:4px;position:relative}
  .gut::after{content:'';position:absolute;left:4px;top:0;bottom:0;width:2px;
    background:var(--line);border-radius:2px}
  .gut:hover::after,.gut.live::after{background:var(--accent)}
  .gut1{grid-area:g1}.gut2{grid-area:g2}
  @media (max-width:1200px){ main{grid-template-columns:1fr;
    grid-template-areas:"stage" "anim" "props"} .gut{display:none} }
  /* the stage: centred, scaling with the window; the height budget leaves room for the
     transport and the frame strip below it */
  .stagebox{position:relative;display:flex;justify-content:center}
  .stagebox canvas{width:auto;height:auto;max-width:100%;
    max-height:calc(100vh - 340px);min-height:240px}
  .fsBtn{position:absolute;top:8px;right:8px;z-index:3;padding:4px 9px;opacity:.85}
  .fsBtn:hover{opacity:1}
  body.fs main{grid-template-columns:1fr;grid-template-areas:"stage"}
  body.fs .col-anim,body.fs .col-props,body.fs .gut,body.fs .col-out{display:none}
  body.fs .stagebox canvas{max-width:calc(100vw - 60px);max-height:calc(100vh - 240px)}
  /* the output drawer: baked + render live collapsed against the right edge and slide out
     on hover - the assembler's JSON drawer, verbatim (min-width, because width loses) */
  .col-out{position:fixed;right:0;top:64px;bottom:16px;width:26px;z-index:40;
    transition:min-width .18s ease;display:flex;flex-direction:column;overflow:hidden}
  .col-out:hover,.col-out:focus-within,.col-out.open{min-width:min(430px,55vw)}
  .col-out .panel{flex:1;display:flex;flex-direction:column;overflow:auto;
    box-shadow:-6px 0 18px rgba(0,0,0,.12);padding-left:34px}
  .col-out pre{max-height:none;flex:1;min-height:120px}
  .drawerTab{position:absolute;left:0;top:0;bottom:0;width:26px;display:flex;
    align-items:center;justify-content:center;cursor:default}
  .drawerTab span{writing-mode:vertical-rl;font-size:11px;color:var(--mut);
    letter-spacing:.08em;user-select:none}""",
    "the grid, the stage, the drawer")

# ---------------------------------------------------------------- sandbox-safe storage keys
rep("""const LIB='bunbun-attach-library';""",
    """const LIB='bunbun-attach-library'+(window.SBX?'--'+window.SBX:'');""",
    "library key honours ?sandbox")
rep("""const KEY='bunbun-attach-autosave';""",
    """const KEY='bunbun-attach-autosave'+(window.SBX?'--'+window.SBX:'');""",
    "autosave key honours ?sandbox")

# ---------------------------------------------------------------- boot-time reshuffle
rep("""<script>
const S=96, PAD=20, FULL=S+PAD*2, Z=5, FIT=16;""",
    """<script>
// Test windows announce themselves with ?sandbox=name and get their own storage keys, so a
// throwaway tab can never overwrite the real autosave. Same rule as the Scene Assembler.
window.SBX=new URLSearchParams(location.search).get('sandbox');
// ---- THE NEW SHAPE: reshuffle the parsed markup into the assembler's three columns.
// Moving nodes keeps their ids and their handlers; nothing below this block changes.
(function(){
  const g=id=>document.getElementById(id);
  const main=document.querySelector('main');
  const bigPanel=main.querySelector(':scope > .panel');
  const side=main.querySelector(':scope > .side');
  const panels=[...side.querySelectorAll(':scope > .panel')];
  const mk=cls=>{ const d=document.createElement('div'); d.className=cls; return d };
  const colAnim=mk('col-anim'), colStage=mk('col-stage'), colProps=mk('col-props');
  const gut1=mk('gut gut1'), gut2=mk('gut gut2');
  main.append(colAnim,gut1,colStage,gut2,colProps);
  colStage.appendChild(bigPanel);
  // left: what the animation IS - name/clip/backdrop, the character, save/load
  colAnim.append(panels[0],panels[1]);
  // right: what is ATTACHED - the stack and the prop editor
  colProps.append(panels[2],panels[3]);
  colAnim.appendChild(panels[4]);            // the retired library, still hidden
  // the last panel splits: save/load stays left, baked+render slide into the drawer
  const outP=panels[5];
  const drawer=mk('col-out');
  drawer.innerHTML='<div class="drawerTab"><span>output &mdash; baked &amp; render</span>'+
    '</div><div class="panel" tabindex="-1"></div>';
  const dPanel=drawer.querySelector('.panel');
  let mv=false;
  [...outP.childNodes].forEach(n=>{
    if(n.nodeType===1 && n.tagName==='LABEL' && /baked output/i.test(n.textContent)) mv=true;
    if(mv) dPanel.appendChild(n);
  });
  colAnim.appendChild(outP);
  document.body.appendChild(drawer);
  drawer.querySelector('.drawerTab').addEventListener('click',
    ()=>drawer.classList.toggle('open'));
  // the stage gets its box and the fullscreen button
  const stage=g('stage'), box=mk('stagebox');
  stage.parentNode.insertBefore(box,stage); box.appendChild(stage);
  const fs=document.createElement('button');
  fs.className='mini fsBtn'; fs.id='fsBtn'; fs.title='fill the window (Esc leaves)';
  fs.innerHTML='&#x26F6;'; box.appendChild(fs);
  fs.onclick=()=>{ document.body.classList.toggle('fs');
    fs.innerHTML=document.body.classList.contains('fs')?'\\u2716':'\\u26F6'; };
  addEventListener('keydown',e=>{ if(e.key==='Escape'&&document.body.classList.contains('fs')){
    document.body.classList.remove('fs'); fs.innerHTML='\\u26F6'; }});
  // dividers: drag for more room, widths remembered - the assembler's wire(), verbatim
  const CKEY='bunbun-anim-tool-columns';
  try{ const w=JSON.parse(localStorage.getItem(CKEY)||'{}');
    if(w.anim)  document.documentElement.style.setProperty('--wAnim', w.anim +'px');
    if(w.props) document.documentElement.style.setProperty('--wProps', w.props+'px'); }catch(e){}
  const wire=(gut,varName,col,fromLeft,slot)=>{
    gut.addEventListener('pointerdown',e=>{
      e.preventDefault(); gut.classList.add('live');
      try{ gut.setPointerCapture(e.pointerId) }catch(err){}
      const startX=e.clientX, startW=col.getBoundingClientRect().width;
      const move=ev=>{ const w=Math.max(230, Math.min(560,
          startW+(fromLeft?1:-1)*(ev.clientX-startX)));
        document.documentElement.style.setProperty(varName, w+'px') };
      const up=()=>{ gut.classList.remove('live');
        gut.removeEventListener('pointermove',move); gut.removeEventListener('pointerup',up);
        try{ const w=JSON.parse(localStorage.getItem(CKEY)||'{}');
          w[slot]=Math.round(col.getBoundingClientRect().width);
          localStorage.setItem(CKEY,JSON.stringify(w)) }catch(err){} };
      gut.addEventListener('pointermove',move); gut.addEventListener('pointerup',up);
    });
  };
  wire(gut1,'--wAnim', colAnim, true, 'anim');
  wire(gut2,'--wProps',colProps,false,'props');
  side.remove();
})();
const S=96, PAD=20, FULL=S+PAD*2, Z=5, FIT=16;""",
    "the reshuffle")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
