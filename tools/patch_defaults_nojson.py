"""Three asks in one pass.

1. Scene Assembler force row: the Adult emotions the scene has no animation for appear as
   "defaults:" buttons wired to the stock clips (the factory + click handler are already in).
2. Kill the JSON viewer in the Scene Assembler - the "device JSON" drawer disappears; the
   pre stays in the DOM because the export path still writes it.
3. Kill the Animation Creation drawer too: baked/render buttons live in the LEFT column so
   nothing is lost, and only the baked-JSON pre is hidden.
"""
import io, os

HERE = os.path.dirname(os.path.abspath(__file__))
done = []


def rep(path, old, new, label):
    global done
    s = io.open(path, encoding="utf-8").read()
    if old not in s:
        print("  SKIP:", label)
        return
    io.open(path + ".tmp", "w", encoding="utf-8").write(s.replace(old, new, 1))
    os.replace(path + ".tmp", path)
    done.append(label)
    print("  ok:", label)


ST = os.path.join(HERE, "src", "scene_tool.html")
AE = os.path.join(HERE, "src", "attach_editor.html")

# ---------------- 1. the default-emotion buttons (anchors avoid the … line) ----------
rep(ST,
    """  const forceHtml = anims.filter(a=>a.on).length
    ? '<span class="hint" style="flex:0 0 auto;align-self:center">make him:</span>'+""",
    """  // THE EMOTIONS THE SCENE DOESN'T HAVE fall back to the stock clips - Adult only for
  // now. They are preview-only: never in anims[], never saved, never exported.
  const defEmotes=Object.keys(SD.clips).filter(c=>/^Adult_/.test(c)
    && !/^Adult_(Idle$|Walk_)/.test(c)
    && !anims.some(a=>a.on && a.clip===c));
  const forceHtml = (anims.filter(a=>a.on).length || defEmotes.length)
    ? '<span class="hint" style="flex:0 0 auto;align-self:center">make him:</span>'+""",
    "defaults: the list")

rep(ST,
    """    : '';
  if(setIf('forceRow', forceHtml))""",
    """    : '';
  const defHtml = (forceHtml && defEmotes.length)
    ? '<span class="hint" style="flex:0 0 auto;align-self:center;margin-left:8px" '+
      'title="the stock clips stand in for emotions this scene has no animation for">'+
      'defaults:</span>'+
      defEmotes.map(c=>`<button class="mini" data-forcedef="${c}" style="opacity:.72">${
        c.replace(/^Adult_/,'')}</button>`).join('')
    : '';
  if(setIf('forceRow', forceHtml+defHtml))""",
    "defaults: the buttons")

# ---------------- 2. the assembler's JSON drawer goes ------------------------------------
rep(ST,
    """  cols.pet.addEventListener('pointerenter',()=>dOpen(true));
  cols.pet.addEventListener('pointerleave',()=>dOpen(false));
  tab.addEventListener('click',()=>dOpen(!cols.pet.classList.contains('open')));
  document.body.appendChild(cols.pet);""",
    """  cols.pet.addEventListener('pointerenter',()=>dOpen(true));
  cols.pet.addEventListener('pointerleave',()=>dOpen(false));
  tab.addEventListener('click',()=>dOpen(!cols.pet.classList.contains('open')));
  document.body.appendChild(cols.pet);
  // KILLED, per Jon: the JSON viewer is gone. The panel stays in the DOM because the
  // device-push path still writes $('out') - it is just never shown again.
  cols.pet.style.display='none';""",
    "assembler JSON drawer hidden")

# ---------------- 3. Animation Creation: no drawer; exports stay in the left column ------
rep(AE,
    """  colAnim.append(panels[0],panels[1]);
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
    ()=>drawer.classList.toggle('open'));""",
    """  colAnim.append(panels[0],panels[1]);
  // right: what is ATTACHED - the stack and the prop editor
  colProps.append(panels[2],panels[3]);
  colAnim.appendChild(panels[4]);            // the retired library, still hidden
  // every export lives in the LEFT column - only the baked-JSON viewer is killed. The pre
  // stays in the DOM because copy/download still read what gets written into it.
  colAnim.appendChild(panels[5]);
  const bakedPre=document.getElementById('out');
  if(bakedPre) bakedPre.style.display='none';""",
    "editor: exports left, JSON pre hidden")

# no drawer -> the tab gutter in main's padding is no longer needed
rep(AE,
    """  main{display:grid;gap:0 10px;padding:16px 34px 16px 16px;align-items:start;""",
    """  main{display:grid;gap:0 10px;padding:16px;align-items:start;""",
    "editor: padding back to plain")

print("written;", len(done), "edits")
