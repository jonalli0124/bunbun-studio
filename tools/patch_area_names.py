"""Areas get real names, and a rename shows up everywhere at once.

Jon: "we also need to rename the areas. the new name isnt showing up for the animations".

An area keeps its lasting number (Z1 - that is what rules bind to) and gains a NAME shown
wherever the area appears: the canvas label, its list row, the rule dropdown ("only in the
reading corner"), and the rule panel. The pencil on its row renames it, same as animations.

The second half was a bug in the animation rename: committing repainted the canvas but never
re-synced the rule panel, so "where <old name> may happen" kept the old name until you
re-selected. One label lagging reads as the rename not working.
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


# one place that answers "what is this area called"
rep("""const areaOf=id=>scene.zones.find(z=>z.kind==='spot'&&z.on!==false&&z.spotId===id);""",
    """const areaOf=id=>scene.zones.find(z=>z.kind==='spot'&&z.on!==false&&z.spotId===id);
// its name everywhere it appears - the number stays (rules bind to it), the name is for people
const areaLabel=z=>z ? (z.name ? z.name : 'Z'+z.spotId) : '?';
const areaLabelById=id=>{ const z=scene.zones.find(q=>q.kind==='spot'&&q.spotId===id);
  return areaLabel(z); };""",
    "one source for area names")

# the canvas label
rep("""      ctx.fillText(spot?('Z'+z.spotId):walk?'walkable':'keep out', cx*Z, cy*Z);""",
    """      ctx.fillText(spot?areaLabel(z):walk?'walkable':'keep out', cx*Z, cy*Z);""",
    "on the canvas")

# the zone list row: the name, plus a pencil for spot zones
rep("""      ? zs.map((z,i)=>{ const nm=z.kind==='floor'?'walkable':z.kind==='spot'?('Z'+z.spotId+' area'):'keep out';""",
    """      ? zs.map((z,i)=>{ const nm=z.kind==='floor'?'walkable'
          :z.kind==='spot'?(areaLabel(z)+(z.name?' (Z'+z.spotId+')':' area')):'keep out';""",
    "in the list")

rep("""          <span class="nm" data-z="${i}">${nm}</span>
          <span class="hint">${(z.pts||[]).length} pts</span>""",
    """          <span class="nm" data-z="${i}">${nm}</span>
          ${z.kind==='spot'?`<button class="mini" data-zren="${i}" title="name this area">&#9998;</button>`:''}
          <span class="hint">${(z.pts||[]).length} pts</span>""",
    "a pencil on area rows")

rep("""    $('zList').querySelectorAll('[data-zon]').forEach(b=>b.onclick=()=>{""",
    """    $('zList').querySelectorAll('[data-zren]').forEach(b=>b.onclick=ev=>{
      ev.stopPropagation();
      const z=scene.zones[+b.dataset.zren];
      const row=b.closest('.anim'), nm=row.querySelector('.nm');
      const inp=document.createElement('input');
      inp.value=z.name||''; inp.placeholder='e.g. reading corner';
      inp.style.cssText='flex:1;font:inherit;padding:2px 6px;border:1px solid var(--accent);'+
        'border-radius:6px;background:var(--bg);color:var(--ink)';
      nm.replaceWith(inp); inp.focus(); inp.select();
      let done=false;
      const commit=()=>{ if(done) return; done=true;
        const n=inp.value.trim(); z.name=n||undefined;
        setTimeout(()=>{ syncRule(); draw() },0) };
      inp.onkeydown=k=>{ if(k.key==='Enter') commit();
        if(k.key==='Escape'){ done=true; setTimeout(()=>draw(),0) } };
      inp.onblur=commit });
    $('zList').querySelectorAll('[data-zon]').forEach(b=>b.onclick=()=>{""",
    "renaming an area")

# the rule dropdown and panel speak the name
rep("""`<option value="${z.spotId}"${a.rule&&a.rule.inArea===z.spotId?' selected':''}>only in Z${z.spotId}</option>`""",
    """`<option value="${z.spotId}"${a.rule&&a.rule.inArea===z.spotId?' selected':''}>only in ${areaLabel(z)}</option>`""",
    "in the rule dropdown")

rep("""    $('ruleWhy').innerHTML = z
      ? (zp&&zp.unusable
         ? `<b>Z${a.rule.inArea} has no room to stand</b> \\u2014 it is off the walkable floor, `+
           `or smaller than his feet. Drag it onto the floor or grow it, and this will play.`
         : `Bound to Z${a.rule.inArea} \\u2014 he does this only inside it.`)""",
    """    $('ruleWhy').innerHTML = z
      ? (zp&&zp.unusable
         ? `<b>${areaLabel(z)} has no room to stand</b> \\u2014 it is off the walkable floor, `+
           `or smaller than his feet. Drag it onto the floor or grow it, and this will play.`
         : `Bound to ${areaLabel(z)} \\u2014 he does this only inside it.`)""",
    "and the panel")

rep("""      spots:p?[{x:p.x,y:p.y,object:'Z'+a.rule.inArea,area:z,unusable:p.unusable}]:[]};""",
    """      spots:p?[{x:p.x,y:p.y,object:areaLabel(z),area:z,unusable:p.unusable}]:[]};""",
    "the sim says it too")

# the name travels: autosave keeps the whole zone object already; the export gains it
rep("""    zones: scene.zones.filter(z=>z.kind==='spot').map(z=>({kind:'spot',spotId:z.spotId,
      pts:z.pts, on:z.on!==false})),""",
    """    zones: scene.zones.filter(z=>z.kind==='spot').map(z=>({kind:'spot',spotId:z.spotId,
      name:z.name||null, pts:z.pts, on:z.on!==false})),""",
    "exported")

rep("""  scene.zones=(s.zones||[]).map(z=>({kind:z.kind,pts:z.pts,spotId:z.spotId}))""",
    """  scene.zones=(s.zones||[]).map(z=>({kind:z.kind,pts:z.pts,spotId:z.spotId,
      name:z.name||undefined, on:z.on}))""",
    "and re-imported")

# the animation-rename half: commit must re-sync the panel, not only repaint
rep("""    const commit=()=>{ if(done) return; done=true;
      const n=inp.value.trim(); if(n) a.name=n;
      setTimeout(()=>draw(), 0); };""",
    """    const commit=()=>{ if(done) return; done=true;
      const n=inp.value.trim(); if(n) a.name=n;
      // draw() repaints, but the rule panel header ("where X may happen") only follows on a
      // sync - without it the old name sat there and the rename looked ignored
      setTimeout(()=>{ syncRule(); draw() }, 0); };""",
    "an animation rename updates the panel")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
