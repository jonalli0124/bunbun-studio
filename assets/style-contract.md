# Style contract — defined by the crocodile (Checkpoint A, 2026-08-17)

Per CHARACTER-PACKS.md §1.1/§7.5: the crocodile is the reference pack; whatever its
Checkpoint A defines is the contract every later species is generated against.
Reference image: `characters/croc/_composite_41_12.png` (head 41 + tail 12 from the
Stage-0 exploration), realised as a full character through the CAPYBARA-PILOT.md pipeline.

## The look

- **Flat minimalist picture-book style of Dick Bruna** (name Bruna, NEVER Miffy — Miffy
  returns a rabbit regardless of species).
- Ultra-clean geometry, **uniform thick black outlines**, no interior detail, no gradients,
  no shadows, no texture. A base+shade pair per region is a PASS, not a defect.
- **Dot-eye motif is absolute**: two small round eyes as solid black dots, set far apart.
  Non-emotion states (idle, walk, eat, bathe, sit, crawl) keep plain dots + short line
  mouth. Emotions + sleep vary the eye shape per the bunny's vocabulary (PILOT §5 table).
- Species identity is carried by silhouette features named in the prompt (for the croc:
  protruding blunt rounded snout, small eye bumps on top of the head, thick tapering tail
  with triangular scutes), never by interior detail.

## Fixed generation settings (PILOT §1 — do not vary)

| | |
|---|---|
| mode | v3 |
| body type | humanoid (upright, bipedal) |
| size | 96 |
| view | **low top-down** |
| detail | low detail |
| outline | single color black outline (default) |

## Palette rules

- **Garment is a keying colour, not the shipped look**: simple flat bright purple A-line
  tunic (adult) / purple onesie (baby). Purple is species-proof — far in RGB from every
  plausible animal colour. Runtime recolours it.
- Body colour must sit far from purple (croc: mid green; base↔base distance must exceed
  ~40 after the --dist 70 clean or the species fails).
- Regions swap as **pairs** (base + shade); fixed black outline never swaps.
- Cleanup: `py clean_sprite.py raw clean --dist 70 --keep-outline --norm 96 --report`,
  one pass per species, marks separately without --norm.

## Canvas conventions

- 96×96 canvas; v3 animations come back expanded (120/124 observed) and are
  **feet-normalised** back to 96 (`--norm 96`). Template output is already 96.
- Walk/crawl generated EAST (+ diagonals as needed), WEST is a PC-side mirror.
- Baby phase is a **render-time ~70% scale**, never a state edit.
