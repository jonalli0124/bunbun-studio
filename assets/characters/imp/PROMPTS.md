# Imp pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
ADULT-ONLY pack. First pack in the SHADED direction (see the note at the bottom).

## OWNER SIGN-OFF 2026-08-21: KEEPER = 98008edc-473a-4633-b216-de3b23cc0e6c "Imp sly brow"

Jon: "Imp sly brow is where it's at."

create_character, **mode "pro"** (25 generations), humanoid, size 96, view "low top-down".
Pro ignores outline/shading/detail, so the whole style rides in the text.

```
pixel art sprite of a small mischievous ghost imp creature standing on two short legs, two short arms and two short legs all four clearly separated from the body silhouette, a rounded head merging directly into a heavy rounded body, body widest at the base, saturated violet body with a paler lilac underside, narrow wedge-shaped eyes angled sharply down toward the centre of the face with a strong slanted brow ridge above each eye, looking straight ahead at the viewer, a wide toothy grin stretching most of the face, an irregular silhouette with a few pointed tufts breaking the outline, the creature fills the frame from top edge to bottom edge, hard dark outline in a deep tinted shade of the violet body and never pure black, a tight palette of about a dozen colours in two hue families with one warm accent against the cool body, banded cel shading with three value steps, separate shade ramps for body and underside, large flat areas of colour, no dithering, solid white background
```

Installed: `Adult_Idle/` = the keeper's 8 rotations, through
`clean_sprite.py --dist 70 --keep-outline`. 8 colours, ink 52x57.
NOT YET DONE: the 12 animations + 2 states (Adult_Sit, Adult_Sleep), the walk
east/west pair, check_pack, mkspecies.

## 2026-08-24 — IP re-roll (owner: "Nudge designs first")

The keeper read as Gengar at a glance (purple round body + pale belly + pointed back
tufts + slanted eyes + wide white toothy grin), flagged against the public-release
direction. Owner chose to nudge the design before animating. New roll "Imp smirk v2",
character 6d667a1b-aefd-4ca3-b427-0b472c2bb7f6, pro mode, humanoid, size 96, low
top-down — the keeper prompt VERBATIM except three recorded swaps that remove the
lookalike markers while keeping the signed-off sly brow and colourway:

- "a wide toothy grin stretching most of the face" -> "a small closed smirk curling up at one corner of the mouth"
- "an irregular silhouette with a few pointed tufts breaking the outline" -> "two long pointed ears drooping outward and down from the sides of the head, a thin pointed tail ending in an arrowhead tip curling out behind the body, a single flame-shaped tuft of hair rising from the crown of the head"
- (nothing else changed)

NOTE: the arrowhead tail makes imp a TAILED SPECIES — the hands-check rule applies to
every animation from here on. PENDING owner review; the 98008edc keeper stays in
PixelLab as fallback until sign-off.

## OWNER RULING 2026-08-24, same day: "let's just [use] the original that we had for both"

The v2 re-roll was REVIEWED AND SET ASIDE. **The original 98008edc "Imp sly brow" stays
the keeper** with the Gengar resemblance flagged and accepted by the owner. 6d667a1b
"Imp smirk v2" stays in PixelLab unused (fallback if the public-release question ever
reopens). Phase 2 runs on 98008edc. The original has no tail — the tailed-species
hands-check note above applies only to the unused v2.

```
pixel art sprite of a small mischievous ghost imp creature standing on two short legs, two short arms and two short legs all four clearly separated from the body silhouette, a rounded head merging directly into a heavy rounded body, body widest at the base, saturated violet body with a paler lilac underside, narrow wedge-shaped eyes angled sharply down toward the centre of the face with a strong slanted brow ridge above each eye, looking straight ahead at the viewer, a small closed smirk curling up at one corner of the mouth, two long pointed ears drooping outward and down from the sides of the head, a thin pointed tail ending in an arrowhead tip curling out behind the body, a single flame-shaped tuft of hair rising from the crown of the head, the creature fills the frame from top edge to bottom edge, hard dark outline in a deep tinted shade of the violet body and never pure black, a tight palette of about a dozen colours in two hue families with one warm accent against the cool body, banded cel shading with three value steps, separate shade ramps for body and underside, large flat areas of colour, no dithering, solid white background
```

## Phase 2 2026-08-24 — croc/canonical prompts on keeper 98008edc, species word "imp"

Same recorded substitutions as the frill pack (the sanctioned pattern): species word
swapped throughout, garment clause DROPPED (no clothes), dance uses the arms wording,
prompts otherwise byte-identical to the frill log — see
assets/characters/frill/PROMPTS.md for the full texts. State face words follow the
species: "the narrow wedge eyes with their slanted brows and the wide toothy grin,
unchanged".

- Adult_Walk group a0d6ede2 (east/north-east/south-east; west halves are PC mirrors)
- Adult_Eat 0f2e54fe · Adult_Bathe 3e8bb920 · Adult_Pickup 656655fe
- Adult_Angry 3f862bfa · Adult_Sick 987ccaa3 · Adult_Bored a29aea21 · Adult_Tired c88f5dbe
- Adult_Love b7079234 · Adult_Hungry 5dc1f8f1 · Adult_Happy c7b3e68c
- Adult_Dance 1eb8def3 · Adult_Play 9c692f14
- Adult_Sit state fb103589 (96) · Adult_Sleep state 6e73d79b (override 120x120), both
  use_color_palette_from_reference=True

## What the rolls before it taught (all superseded, kept for the record)

- v3 mode produced soft blobby output no wording could fix. **pro is the default for a
  shaded character**; v3 was only ever right for the FLAT Bruna cast.
- "friendly / small / round / big round eyes / held at the sides" are CUTENESS
  instructions. Three accidental baby animals before that was spotted.
- "half-lidded" reads as stoned, not sly. Mischief is the **brow**: a slanted ridge and a
  wedge eye angled down toward the centre. Jon: "looks...stoned and not quite mischievous".
- Pure black in the palette is what made the earlier rolls look muddy next to Frill —
  the darkest tone must be a deep TINT of the body hue.
- Raw generator output looks AI-made because of 25-50 near-duplicate shades.
  `clean_sprite.py` is part of the style, not a polish step.

## 2026-08-25 (late 8/24 night) — Baby Onesie preview roll — [PENDING owner review]

Character 218a9c2a ("Imp Baby Onesie"): pro 25 gens, style_character=Adult_Sit state fb103589 (keeper deleted), composed from recorded appearance.
The croc-pilot baby clauses folded in verbatim: "with the head noticeably larger
relative to the body, about half the total height, and short stubby arms and legs" +
"Wearing a simple flat bright purple onesie, one piece covering the whole body, short
sleeves and short legs." South idle shown in the 8/24 nursery sheet; nothing installed.

## 2026-08-25 (overnight) — THE BABY SET, owner-signed (states on the keeper, capybara model)

Target structure (owner): 1 character, 3 adult states, 4 baby states; baby walk is CRAWL.
Baby_Onesie e49c7550 = a WARDROBE STATE on the keeper (the earlier standalone-create
babies were owner-rejected and deleted: "i expected you to create states of the main
character"). Onesie clause, owner-ruled FOOTED ("make sure the onesie doesnt show their
feet. it is to add to the cuteness"): "Wearing a simple flat bright purple onesie, one
piece covering the whole body, short sleeves, and closed footed legs covering the feet
completely like footed pajamas." Derived from the signed onesie with the species face
words and the footed-hold clause "whose closed footed legs still cover the feet
completely" (proven on capybara): Baby_Sit bbdad6ca (96) · Baby_Sleep f034103a
(override 120x120, the closed-eye clause) · Baby_Crawl 8a9bec33 (96). All
use_color_palette_from_reference=True except the onesie itself. Reviewed in the 8/25
grid sheets; baby SIZE is render-time scale, never art. Nothing installed to the pak yet.

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
baby emotes: play 0c0295c3 · hungry 9e6fd6e0 · love 600ee460 · tired 93a78e8b · bored bb7cb83e · sick 37bbe043 · angry 7c1353bf · bathe 57b21d61 · eat 31bcb66f
baby happy (hop-overhead, canonical, awaiting sign-off): d9d83fbe
baby crawl (SE+NE diagonals; stray east dirs deleted 8/25): 2e517206

## 8/25 audit note: the Phase-2 adult anim sources went down with the deleted Idle keeper
98008edc — the installed frames in this pack are the only copy. A future re-roll needs the
planned Idle keeper rebuild first.

## 2026-08-25 evening: Baby_Happy INSTALLED (owner signed the review page) - the baby set is TEN emotions.

## 2026-08-25 late: THE TEEN (owner-signed lineup). Teen_Idle state 210c0a9b (v2, on Adult_Sit source, standing clause)
on the keeper: short-sleeved bright orange t-shirt, fur/feathers slightly disheveled
with small tufts on the crown, and (no ears - scruffy tufts). The V2 spiky-tuft look IS the ruling
(soft/contained variants lost). INSTALLED as Teen_Idle/ (8 rotations, feet y=90);
ships via the mkspecies teen-idle toehold. Teen size stays render-time SCALE_TEEN.

## 2026-08-25 late: WARDROBE -> LIGHT BLUE (owner order "set all clothes to light
blue"): the anchored pair is now #82c4ea/#4c7fb5 (was #9151d3/#5d229d). Adults and
onesies swapped together; teens stay orange; imp exempt. Exact both ways.

## 2026-08-25 late: THE ONESIE EXEMPTION ENDS (owner: "Onesie imp"). Measured first:
the magenta family (#b53ce6/#b040de brights + 11 fragmented dark shades) appears in ZERO
pixels outside Baby_ clips - it was never his body, just his onesie. All 122 baby frames
re-paired onto the anchored wardrobe pair #82c4ea/#4c7fb5, unifying the near-twin brights
as base and the dark cluster as shade. His purple BODY stays untouched and un-swappable.

## 2026-08-26 (small hours): THE BLUE LINEAGE (owner: "I just want a color that can be
changed like the other one"). The two-tone suit was unfixable in place (the magenta
onesie had whole regions painted in body purple - the true face of the old exemption),
so the baby lineage was RE-ROLLED with "light blue onesie" in the proven verbatim
wording. New chars: Baby_Onesie_blue 4bfcc210 (from Adult_Sit fb103589, footed clause) ·
Baby_Sit_blue c58c03e7 · Baby_Sleep_blue 2e397c57 (120x120, closed-eye clause) ·
Baby_Crawl_blue afd9e5ae. Emotes x10 re-fired byte-identical EXCEPT the outfit clause
now tells the truth ("Its light blue onesie remains fixed in place"): eat fc3da27a ·
bathe 9c50ae37 · angry 54a07b96 · sick c2813f1c · bored 4c5a4685 · tired 56a5e1a2 ·
love d2686f12 · hungry f6d99093 · play 78a07401 · happy 38ecb35e. Crawl SE+NE cc732e11
(SW/NW mirrored, as-generated per the owner orientation table). Roll pair #8ed5f5/#589ec5
(+twin shade #5c9bbf) anchored EXACTLY onto #82c4ea/#4c7fb5 at install - THE IMP ONESIE
NOW RECOLOURS WITH THE FLEET. The old magenta-lineage chars survive on PixelLab until
the owner signs the new set.

## 2026-08-26: owner signed the blue lineage ("I am good with it") - the magenta
lineage DELETED from PixelLab (onesie e49c7550 + its 10 emote anims, sit bbdad6ca,
sleep f034103a, crawl 8a9bec33 + 2 crawl anims). The blue chars (4bfcc210 onesie,
c58c03e7 sit, 2e397c57 sleep, afd9e5ae crawl) are now the ONLY imp baby lineage.
Adult_Sit fb103589 (style source) and the rebuilt Idle 4d73ef57 both KEPT.

## 2026-08-26: THE TEEN SET COMPLETE (owner signed the sheets: "Go for it").
Idle 210c0a9b · Teen_Sit bc40e367 · Teen_Sleep 0dc5c2ef (120x120, closed-eye clause).
Anims (v3 south fc6 on the idle char; walk 4 dirs E/NE/SE/NW fc6, W+SW mirrored):
eat d8f58347 · bathe 60cecd1f · angry b59bdb63 · sick 9509dd26 · bored 8e23d123 · tired 80c967bc · love 592db9f2 · hungry 276a69f6 · play 0c354077 · happy 1a46b6ad · dance 9a097c4d · pickup 0f6616b5 · walk 2ffa98ce
Tee anchored to the teen wardrobe pair #fd9a05/#df5b0f; fur twins unified onto the adult
palette; PROOF: zero pair pixels outside Teen_ clips fleet-wide. Roster: 21 clips, a
mirror of count and type of the adult set.
