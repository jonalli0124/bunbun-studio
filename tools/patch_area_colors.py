"""Each activity area gets its own colour the moment it is drawn.

Jon: "can you make the area creation color coded to make the ones created?" - so two areas
never read as one. The colour keys off the lasting spotId, so it never changes once assigned,
and it shows everywhere the area does: the shape, its label, and its list chip.
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


rep("const areaLabel=z=>z ? (z.name ? z.name : 'Z'+z.spotId) : '?';",
    "const areaLabel=z=>z ? (z.name ? z.name : 'Z'+z.spotId) : '?';\n"
    "// every area owns a colour from the moment it is drawn, keyed to its lasting number -\n"
    "// so it never changes, and two areas never read as one\n"
    "const AREA_HUES=[265,195,330,20,160,45,300,220];\n"
    "const areaColor=(z,a)=>'hsla('+AREA_HUES[(((z&&z.spotId)||1)-1)%AREA_HUES.length]+\n"
    "  ',72%,52%,'+(a==null?1:a)+')';",
    "a lasting colour per area")

rep("""    ctx.fillStyle=walk?'rgba(60,180,90,0.15)':spot?'rgba(145,81,211,0.16)':'rgba(240,130,30,0.20)';
    ctx.fill();
    ctx.strokeStyle=walk?'rgba(60,180,90,0.9)':spot?'rgba(145,81,211,0.9)':'rgba(240,130,30,0.9)';""",
    """    ctx.fillStyle=walk?'rgba(60,180,90,0.15)':spot?areaColor(z,0.16):'rgba(240,130,30,0.20)';
    ctx.fill();
    ctx.strokeStyle=walk?'rgba(60,180,90,0.9)':spot?areaColor(z,0.9):'rgba(240,130,30,0.9)';""",
    "drawn in its own colour")

rep("""      ctx.fillStyle=walk?'rgba(60,180,90,1)':spot?'rgba(145,81,211,1)':'rgba(240,130,30,1)';""",
    """      ctx.fillStyle=walk?'rgba(60,180,90,1)':spot?areaColor(z,1):'rgba(240,130,30,1)';""",
    "its label too")

rep("""            background:${z.kind==='floor'?'rgba(60,180,90,.9)':z.kind==='spot'
              ?'rgba(145,81,211,.9)':'rgba(240,130,30,.9)'}\"""",
    """            background:${z.kind==='floor'?'rgba(60,180,90,.9)':z.kind==='spot'
              ?areaColor(z,.9):'rgba(240,130,30,.9)'}\"""",
    "and its list chip")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
