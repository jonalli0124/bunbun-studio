"""Stop the spinning outright, and stop him squeezing behind furniture.

SPINNING. Every fix so far has been geometric - "only flip when the move is mostly horizontal",
"only when it is bigger than 3px" - and geometry keeps finding a new way to dither: a waypoint
that doubles back, a face left stale by chooseNext and re-derived on the next frame, a big TEMPO
step that overshoots. So this stops arguing with the geometry and puts a floor on it: HIS FACING
MAY CHANGE AT MOST ONCE A SECOND. A pet that turns round twice in one second looks broken no
matter how well justified each turn was.

BEHIND THE CHAIR. The route was searched over walkable() - a single pixel test on his feet - so a
4px gap between a keep-out and the wall counted as a corridor, and he threaded it, which puts him
behind the chair. Endpoints already had to be standable(); now every cell of the route does. He
can only walk where he could stand.
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


# ---------------------------------------------------------------- no squeezing through gaps
rep("""  const ok=new Uint8Array(W*H);
  for(let j=0;j<H;j++)for(let i=0;i<W;i++) ok[j*W+i]=walkable(px(i),py(j))?1:0;""",
    """  const ok=new Uint8Array(W*H);
  // standable(), not walkable(). A 4px slot between a keep-out and the wall passes a
  // single-pixel test and reads as a corridor - he threads it and ends up behind the chair.
  // He may only walk where he could stand.
  for(let j=0;j<H;j++)for(let i=0;i<W;i++) ok[j*W+i]=standable(px(i),py(j))?1:0;""",
    "route only where he could stand")

rep("""function pathBlocked(ax,ay,bx,by){
  const d=Math.hypot(bx-ax,by-ay), n=Math.max(2,Math.ceil(d/3));
  for(let i=1;i<n;i++){ const t=i/n;
    if(!walkable(ax+(bx-ax)*t, ay+(by-ay)*t)) return true }
  return false;
}""",
    """function pathBlocked(ax,ay,bx,by){
  const d=Math.hypot(bx-ax,by-ay), n=Math.max(2,Math.ceil(d/3));
  for(let i=1;i<n;i++){ const t=i/n;
    // same rule as the grid: room to stand, not a single legal pixel
    if(!standable(ax+(bx-ax)*t, ay+(by-ay)*t)) return true }
  return false;
}""",
    "line of sight uses the same rule")

# standable() is used above findRoute now, so it has to be defined before it
rep("""// Room to STAND, not just a walkable pixel.""",
    """// Defined above the router on purpose - the router asks this question for every cell.
// Room to STAND, not just a walkable pixel.""",
    "note the ordering")

# ---------------------------------------------------------------- one turn a second, at most
rep("""      // main.cpp:2554 picks the clip by whether the trip is mostly horizontal:
      //   bool horiz = fabsf(dx) > fabsf(dy);
      // Facing off the sign of dx alone means a mostly-VERTICAL step flips east/west every
      // frame, which is the twirling. Vertical movement keeps whatever he was already facing.
      if(Math.abs(dx)>Math.abs(dy) && Math.abs(dx)>1) sim.face=dx<0?-1:1;
      sim.walkClip=walkClipFor(sim.a, sim.face||1);""",
    """      // main.cpp:2554 picks the clip by whether the trip is mostly horizontal:
      //   bool horiz = fabsf(dx) > fabsf(dy);
      // That is necessary but not sufficient. Geometry keeps finding new ways to dither - a
      // waypoint that doubles back, a stale face re-derived next frame - so there is also a
      // hard floor: HE MAY TURN AT MOST ONCE A SECOND. Whatever the maths wants, a pet that
      // spins on the spot is wrong, and no justification for an individual turn changes that.
      const want = (Math.abs(dx)>Math.abs(dy) && Math.abs(dx)>1) ? (dx<0?-1:1) : (sim.face||1);
      if(sim.face==null) sim.face=want;
      else if(want!==sim.face){
        sim.faceHold=(sim.faceHold||0)+dt;
        if(sim.faceHold>=TURN_HOLD_S){ sim.face=want; sim.faceHold=0 }
      } else sim.faceHold=0;
      sim.walkClip=walkClipFor(sim.a, sim.face||1);""",
    "at most one turn a second")

rep("""const WAIT_MIN=18, WAIT_MAX=42; // main.cpp:2538  18-42s between journeys""",
    """const WAIT_MIN=18, WAIT_MAX=42; // main.cpp:2538  18-42s between journeys
// Not from the firmware - the firmware has four directions and never faces this problem. This
// is the floor under the facing rule, in GAME seconds, so TEMPO does not shorten it.
const TURN_HOLD_S=1.0;""",
    "the turn-hold constant")

# and never leave a stale facing behind for the next journey to flip
rep("""       settle, face:0, walkClip:walkClipFor(c.a, to.x<from.x?-1:1), says:""",
    """       settle, face:(to.x<from.x?-1:1), faceHold:0,
       walkClip:walkClipFor(c.a, to.x<from.x?-1:1), says:""",
    "set out already facing the right way")

rep("""    sim.walkClip=walkClipFor(sim.a, sim.tx<sim.x?-1:1);
    return;
  }""",
    """    sim.face=(sim.tx<sim.x?-1:1); sim.faceHold=0;   // and the same for a wander
    sim.walkClip=walkClipFor(sim.a, sim.face);
    return;
  }""",
    "wanders set out facing correctly too")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
