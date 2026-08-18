"""Three columns that follow the work, instead of one long right-hand scroll.

Jon: "redo the layout of the scene assembler so it is more logical and not all just on the right
side". The work has an order - build the room, then teach the pet, then set the mood - and the
page now reads in that order, left to right:

    THE ROOM               THE STAGE                THE PET
    room picker            canvas + transport       animations + where-rules
    put something in it                             watching (pace/tempo/clock)
    the ground             what the device is told  travel size
    light & weather        (under the canvas,       passive scale
                            full width)

Same panels, same ids, no behaviour change - they are MOVED, not rebuilt, so every handler keeps
working. On a narrow window the columns stack in the same order the work happens.
"""
import io, os, re

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


rep("""  main{display:flex;gap:16px;padding:16px;align-items:flex-start;flex-wrap:wrap}""",
    """  main{display:grid;gap:16px;padding:16px;align-items:start;
       grid-template-columns:minmax(280px,340px) minmax(420px,1fr) minmax(300px,360px);
       grid-template-areas:"room stage pet"}
  @media (max-width:1100px){ main{grid-template-columns:1fr;
       grid-template-areas:"stage" "room" "pet"} }
  .col-room{grid-area:room} .col-stage{grid-area:stage} .col-pet{grid-area:pet}
  .col-room,.col-pet,.col-stage{display:flex;flex-direction:column;gap:13px;min-width:0}""",
    "a three-column grid")

# the page is re-plumbed by moving whole panels between columns AFTER load - the markup order
# stays exactly as it is, so no string surgery on panel bodies is needed at all
rep("""</main>""",
    """</main>
<script>
// THE LAYOUT IS PLUMBING, DONE ONCE AT LOAD. Panels are MOVED (appendChild keeps every
// listener), never rebuilt. Each panel is found by a stable element inside it, so reordering
// the source never breaks this.
(()=>{
  const main=document.querySelector('main');
  const holder=id=>{ const e=document.getElementById(id);
    return e ? e.closest('.panel') : null };
  const cols={};
  for(const k of ['room','stage','pet']){
    cols[k]=document.createElement('div'); cols[k].className='col-'+k;
  }
  const canvasPanel=document.getElementById('view').closest('.panel');
  const plan=[
    ['room', holder('room')],          // the room + put something in it + the ground
    ['room', holder('tod')],           // light & weather set the room's mood
    ['stage', canvasPanel],
    ['stage', holder('out')],          // what the device is told, under the canvas
    ['pet',  holder('anims')],         // animations + where-rules
    ['pet',  holder('pace')],          // watching: pace / tempo / clock + travel + passive
  ];
  for(const [k,p] of plan) if(p && !p.dataset.placed){ p.dataset.placed=1; cols[k].appendChild(p) }
  // anything not named above keeps its old order, into the pet column last
  for(const p of [...main.querySelectorAll(':scope > .panel, :scope > .side > .panel')])
    if(!p.dataset.placed){ p.dataset.placed=1; cols.pet.appendChild(p) }
  const side=main.querySelector(':scope > .side'); if(side) side.remove();
  main.append(cols.room, cols.stage, cols.pet);
})();
</script>""",
    "panels are moved, not rebuilt")

# the watching panel needs a findable id: `pace` is the select inside it
tmp = SRC + ".tmp"
io.open(tmp, "w", encoding="utf-8").write(s)
os.replace(tmp, SRC)
print("written;", len(done), "edits")
