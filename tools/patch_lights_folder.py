"""A third kind of object: lights.

Anything in assets/objects/lights/ is a light fixture. Placing one turns the light ON without
hunting for a button, which is the discoverability problem with "select an object, then press
make light". The old button stays, so any object can still become a lamp.

Also: rain darkens the SKY at any hour. The builder's rain term is rainAmount*daylight*0.14 and
it applies indoors only, so a shower at night changed nothing and the window stayed bright blue
behind the drops. A rain sky is overcast whatever the clock says.
"""
import io, os

TOOLS = os.path.dirname(os.path.abspath(__file__))


def patch(path, subs):
    s = io.open(path, encoding="utf-8").read()
    done = []
    for old, new, label in subs:
        if old not in s:
            print("  SKIP:", label)
            continue
        s = s.replace(old, new, 1)
        done.append(label)
        print("  ok:", label)
    tmp = path + ".tmp"
    io.open(tmp, "w", encoding="utf-8").write(s)
    os.replace(tmp, path)   # always write what succeeded
    return done


# ---- the data builders learn the third folder ----
patch(os.path.join(TOOLS, "mkdata.py"), [(
    "for kind in ('marks','props'):",
    "for kind in ('marks','props','lights'):", "scan lights/")])
patch(os.path.join(TOOLS, "mkscene.py"), [(
    'for kind in ("marks", "props"):',
    'for kind in ("marks", "props", "lights"):', "scan lights/")])

SRC = os.path.join(TOOLS, "src", "attach_editor.html")
patch(SRC, [
    # a light placed in the room IS a light
    ("""$('rpNew').onclick=()=>{ const n=$('rpAdd').value; if(!DATA.props[n]) return;""",
     """$('rpNew').onclick=()=>{ const n=$('rpAdd').value; if(!DATA.props[n]) return;
  const isLight=(DATA.props[n].kind==='light');""",
     "detect a fixture"),
    ("""  const p=rpDefaults(n); p.scale=+(24/Math.max(d.w,d.h)).toFixed(2);   // start about 24px
  rprops.push(p); rsel=rprops.length-1; rpSync(); render(); };""",
     """  const p=rpDefaults(n); p.scale=+(24/Math.max(d.w,d.h)).toFixed(2);   // start about 24px
  if(isLight){ p.lamp=true; p.lampPower=1; p.lampSize=1; p.y=64; litCache.key='' }
  rprops.push(p); rsel=rprops.length-1; rpSync(); render(); };""",
     "placing a fixture turns it on"),

    # the room picker offers fixtures as well as things
    ("""  $('rpAdd').innerHTML=Object.keys(DATA.props)
    .filter(k=>(DATA.props[k].kind||'prop')==='prop')
    .map(k=>`<option>${k}</option>`).join('');""",
     """  const byKind=w=>Object.keys(DATA.props).filter(k=>(DATA.props[k].kind||'prop')===w)
    .map(k=>`<option>${k}</option>`).join('');
  $('rpAdd').innerHTML=
    `<optgroup label="things">${byKind('prop')}</optgroup>`+
    `<optgroup label="lights">${byKind('light')}</optgroup>`;""",
     "fixtures in the room picker"),

    # and stay out of the character-attach picker's way, but keep them reachable
    ("""  $('addProp').innerHTML=
    `<optgroup label="feelings (over the head)">${group('mark')}</optgroup>`+
    `<optgroup label="things">${group('prop')}</optgroup>`;""",
     """  $('addProp').innerHTML=
    `<optgroup label="feelings (over the head)">${group('mark')}</optgroup>`+
    `<optgroup label="things">${group('prop')}</optgroup>`+
    `<optgroup label="lights">${group('light')}</optgroup>`;""",
     "fixtures in the attach picker"),

    # RAIN DARKENS THE SKY AT ANY HOUR
    ("""    if(out){
      const sky=out===1;
      const nr=sky?16:24, ng=sky?20:28, nb=sky?90:74;
      const amt=night*(sky?1:0.88);
      R+=(nr-R)*amt; G+=(ng-G)*amt; B+=(nb-B)*amt;""",
     """    if(out){
      const sky=out===1;
      const nr=sky?16:24, ng=sky?20:28, nb=sky?90:74;
      const amt=night*(sky?1:0.88);
      R+=(nr-R)*amt; G+=(ng-G)*amt; B+=(nb-B)*amt;
      // A rain sky is overcast whatever the clock says. The builder's rain term is
      // rainAmount*daylight*0.14 and applies INDOORS only, so a shower left the window
      // bright blue behind its own drops. Pull the sky toward slate, on top of the hour.
      if(raining){
        const gr=sky?86:78, gg=sky?94:86, gb=sky?104:92;
        const w=0.45*(1-night*0.5);        // strongest by day, still present at dusk
        R+=(gr-R)*w; G+=(gg-G)*w; B+=(gb-B)*w;
      }""",
     "rain darkens the sky"),
])
print("done")
