"""Take the builder's three speed controls, verbatim, instead of my one.

placer.html's preview group separates three things I had mashed into a single "PACE" slider, and
the separation is the whole point:

  pace    "how often bunbun decides to move. as shipped = the firmware's own numbers"
          1 as shipped / 3 lively / 6 restless      -> BEHAVIOUR: scales only the decision clock
  tempo   "watching speed - does not change the scene"
          1 real / 6 / 20 / 60                      -> WATCHING: time itself runs faster, so the
                                                       walk covers 42px per GAME second still
  day     "how fast the clock runs"
          1 real / 60 day-in-24-min / 180 day-in-8  -> the time of day advances while you watch

Same ids, same option values, same wording, same defaults (tempo 6, day 60, pace 1) so the two
tools read alike and a number means the same thing in both.
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


# ---------------------------------------------------------------- the controls
rep("""      <label style="margin-top:10px">preview runs <span id="pcl">8</span>&times; real time
        <span class="hint">&mdash; the device waits 18-42s between journeys</span></label>
      <input type="range" id="pc" min="1" max="20" step="1" value="8" style="width:100%">
""",
    """      <label style="margin-top:10px">watching</label>
      <select id="pace" title="how often bunbun decides to move. as shipped = the firmware's own numbers"
              style="width:100%">
        <option value="1" selected>bunbun: as shipped</option>
        <option value="3">bunbun: lively (3&times;)</option>
        <option value="6">bunbun: restless (6&times;)</option>
      </select>
      <div class="row" style="margin-top:5px">
        <select id="tempo" title="watching speed - does not change the scene" style="flex:1">
          <option value="1">1&times; real</option>
          <option value="6" selected>6&times;</option>
          <option value="20">20&times;</option>
          <option value="60">60&times;</option>
        </select>
        <select id="day" title="how fast the clock runs" style="flex:1">
          <option value="1">clock: real</option>
          <option value="60" selected>clock: day in 24 min</option>
          <option value="180">clock: day in 8 min</option>
        </select>
      </div>
      <div class="hint">the same three the builder uses. Only <b>bunbun:</b> changes how he
        behaves; the other two change how fast you watch it.</div>
""",
    "the builder's three controls")

# ---------------------------------------------------------------- what they drive
rep("""// He IS that still - that is the charter. This scales only the CLOCKS so a preview is
// watchable; the rules and the constants above are untouched.
let PACE=8;
const waitSecs=()=>(WAIT_MIN+Math.random()*(WAIT_MAX-WAIT_MIN))/PACE;""",
    """// placer.html's preview group, verbatim. PACE is his decision rate - "as shipped" is 1, and
// the firmware's numbers are what he then obeys. TEMPO is watching speed and touches nothing
// but the clock feeding the sim. DAYRATE runs the time of day while you watch.
let PACE=1, TEMPO=6, DAYRATE=60;
const waitSecs=()=>(WAIT_MIN+Math.random()*(WAIT_MAX-WAIT_MIN))/PACE;""",
    "three variables, not one")

rep("""$('pc').oninput=e=>{ PACE=+e.target.value; $('pcl').textContent=PACE; };""",
    """$('pace').onchange=e=>{ PACE=+e.target.value };
$('tempo').onchange=e=>{ TEMPO=+e.target.value };
$('day').onchange=e=>{ DAYRATE=+e.target.value };""",
    "wire all three")

# TEMPO speeds up TIME, so it belongs at the point dt enters the sim - one place, and the
# walk still covers 42px per game-second.
rep("""function stepSim(dt){
  if(!simOn) return;""",
    """function stepSim(realDt){
  if(!simOn) return;
  // "watching speed - does not change the scene": time runs faster, the rules do not change.
  // Capped so a 60x tab that has been in the background does not teleport him across the room.
  const dt=Math.min(0.5, realDt*TEMPO);""",
    "tempo speeds up time itself")

# and the clock runs while you watch
rep("""    const wasSaying=sim&&sim.says;
    stepSim(dt);""",
    """    // "how fast the clock runs": 60 puts a whole day in 24 minutes, 180 in 8.
    if(simOn && DAYRATE>0){
      todMin=(todMin + dt*DAYRATE/60) % 1440;
      const shown=Math.floor(todMin);
      if(shown!==_lastTod){ _lastTod=shown; $('tod').value=shown; $('todl').textContent=hhmm(shown) }
    }
    const wasSaying=sim&&sim.says;
    stepSim(dt);""",
    "the day passes while you watch")

rep("""let booting=true, saveTimer=0;""",
    """let booting=true, saveTimer=0, _lastTod=-1;""",
    "remember the minute last shown")

tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
