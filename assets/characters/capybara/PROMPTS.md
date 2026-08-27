# Capybara pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.

NOTE (2026-08-23): this file did not exist until now. The capybara was the PILOT pack and its
original prompt log lives OUTSIDE the repo at C:\Users\Jon\capybara-pilot — everything before
the entry below is recorded there, not here. Keeper on PixelLab is
`46e34ac6-1c18-40e5-a846-3de399c11ecd` ("Capybara Adult", 8dir 96x96, state:Idle, group +7).

## 2026-08-23 — Adult_Dance (audit fill-in; the clip `dance/anim` really wants)

Keeper `46e34ac6-1c18-40e5-a846-3de399c11ecd`, v3, south only, frame_count 6 (7 stored),
1 generation. Animation group `c4c5d5f6-cfa5-4ae6-81eb-330ac164c346`.

The penguin's verified dance prompt with the species word substituted, plus the one body-part
swap "flipper wings" -> "arms".

```
The capybara stands upright and dances on the spot, bouncing rhythmically from one foot to the other while both arms lift and swing outward to the sides in time with the bounce, the head bobbing gently with each beat, then returning to the neutral upright posture so the motion repeats seamlessly. Empty hands, nothing in the mouth, no props.
```

Install: raw came back **120x120**. Cleaned `--dist 70 --keep-outline`, NO `--norm`. Shared
offset dx=12 dy=12 for all 7 frames, anchored on the clip's LOWEST extent so nothing sits below
this pack's floor row 89. Zero opaque px clipped. Lift 7px. This pack came back ALREADY carrying
the anchor purples #9151d3/#5d229d and the canonical fur pair, so no garment swap was needed.
check_pack: PASS, anchor purples missing in 0 frames.

## 2026-08-23 — Adult_Play (audit fill-in; `play/anim` was substituting an emote)

Keeper `46e34ac6-1c18-40e5-a846-3de399c11ecd`, v3, south, frame_count 6 (7 stored). Group `d6841158-1944-462e-be1f-36814f761174`.
Structure reuses the cat's recorded Adult_Play scaffolding (crouch -> wiggle -> spring -> settle,
"Keep the face exactly identical to the source", garment-fixed clause) with the action changed;
the cat's own clip is a cat-specific batting/pouncing move that does not transfer verbatim.

```
The capybara crouches down slightly, wiggles briefly, then springs back up and claps its hands together quickly several times in front of its chest while bouncing on the spot, then settles back into its resting posture. Keep the face exactly identical to the source. Its purple outfit remains fixed in place. Empty hands, nothing held, no props.
```

**ROLL 1 FAILED — THE NAMING-SUMMONS DOCTRINE, EXACTLY.** The first prompt said "bats at the AIR"
and "Empty hands, no props", and the generator summoned props anyway: the dog got a cream ball on his
chest (frame 3) and a small purple object in his raised paw (frames 4-5); the capybara got a tan object
at its hand (frame 4). Naming an action that IMPLIES a target is enough - you do not have to name the
toy. Roll 2 replaced the batting with CLAPPING, which implies no target, and came back clean. Do NOT
add "no ball" to the prompt: naming it summons it.

Install: cleaned `--dist 70 --keep-outline`, no `--norm`; one shared offset anchored on the
clip's lowest extent onto this pack's floor row; zero opaque px clipped; garment mapped as an
explicit pair to #9151d3/#5d229d. check_pack PASS.

## 2026-08-25 — THE BABY EMOTES (v3, south, fc6, on the Baby_Onesie character)

Per the croc-pilot rule, the emote prompts ran BYTE-IDENTICAL across adult and baby —
the species' own recorded texts (with their documented face-word adaptations: dog's
smile mouth-hold in bathe, spark/imp's narrow-eye lines in hungry/love, frill's
dropped outfit clause) fired on the Baby_Onesie character, whose onesie and palette
carry through the animation automatically. The baby set is the croc-proven eight
(eat, bathe, angry, sick, bored, tired, love, hungry) plus play (each species' own
object-free play). One server-side loss (spark bored) re-fired byte-identical.
Reviewed in "The Baby Emotions" artifact; nothing installed to the pak yet.

## Group-id ledger (2026-08-25 audit — every live group on the Baby_Onesie char):
baby emotes: play 615d9889 · hungry e51b0e4f · love fa24dee2 · tired 5d6ee076 · bored f78bf9ff · sick 5acafd09 · angry a1883893 · bathe d2db2313 · eat ebc3def4
baby happy (hop-overhead, canonical, awaiting sign-off): 48956e32
baby crawl (SE+NE diagonals; stray east dirs deleted 8/25): 95776159

## 8/25 audit: Adult_Play roll 1 (77baf233, the naming-summons tan-object roll) DELETED
from PixelLab; v2 d6841158 is the keeper. The 2026-08-16 pilot-era groups (walk_v3 f95fbc77,
eat/bathe/pickup, *_face emotes) are KEPT as the probable sources of the installed adult art.

## 2026-08-25 evening: Baby_Happy INSTALLED (owner signed the review page) - the baby set is TEN emotions.

## 2026-08-25 evening: Adult_Happy 4de20ae9 (canonical hop, first roll) INSTALLED - the pack is 21 adult clips at last.

## 2026-08-25 late: THE TEEN (owner-signed lineup). Teen_Idle state 36815f31 (v2, small round ear folded flat)
on the keeper: short-sleeved bright orange t-shirt, fur/feathers slightly disheveled
with small tufts on the crown, and one small round ear folded flat. The V2 spiky-tuft look IS the ruling
(soft/contained variants lost). INSTALLED as Teen_Idle/ (8 rotations, feet y=90);
ships via the mkspecies teen-idle toehold. Teen size stays render-time SCALE_TEEN.

## 2026-08-25 late: WARDROBE -> LIGHT BLUE (owner order "set all clothes to light
blue"): the anchored pair is now #82c4ea/#4c7fb5 (was #9151d3/#5d229d). Adults and
onesies swapped together; teens stay orange; imp exempt. Exact both ways.

## 2026-08-26: THE TEEN SET COMPLETE (owner signed the sheets: "Go for it").
Idle 36815f31 · Teen_Sit 86369b49 · Teen_Sleep 728892b7 (120x120, closed-eye clause).
Anims (v3 south fc6 on the idle char; walk 4 dirs E/NE/SE/NW fc6, W+SW mirrored):
eat c583e141 · bathe 8181edd9 · angry 3b2d7885 · sick 0c3f5497 · bored 6ba0b2ae · tired 9b0a6ef0 · love 572ebaad · hungry 4027f4da · play 91bf1f35 · happy 4e03bcc0 · dance adb23b90 · pickup 5b080e44 · walk 39147e5e
Tee anchored to the teen wardrobe pair #fd9a05/#df5b0f; fur twins unified onto the adult
palette; PROOF: zero pair pixels outside Teen_ clips fleet-wide. Roster: 21 clips, a
mirror of count and type of the adult set.
