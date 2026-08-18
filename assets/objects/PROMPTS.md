# Items & emotion marks — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
Status marks: [PASS] verified good, [FAIL] rejected (kept for the record).
First-run session 2026-08-17.

House pattern for props (pixflux, no_background=true, outline "single color black
outline", shading "basic shading"): *"soft pixel art, warm colors, clean dark outline,
children's game item, a single <object>, front view, no floor shadow"*. Lessons: white
porcelain objects go pink/tiny under "warm colors" — drop it and add "bright white with
soft gray shading, big and centered filling the canvas"; the toilet only worked in pixen.
Marks pattern (pixen 48x48, no_background=true, outline "single color black outline"):
*"bold pixel art emotion icon for a children's game, <shape>, clean dark outline, simple
and readable at tiny size"*.

## Bathroom set

### towel rack — [PASS, 1 roll] pixflux 96x96
```
soft pixel art, warm colors, clean dark outline, children's game item, a single wooden wall towel rack with a soft blue towel hanging over the bar, front view, no floor shadow
```

### bath mat — [PASS, 1 roll] pixflux 96x48
```
soft pixel art, warm colors, clean dark outline, children's game item, a single fluffy oval bath mat rug in soft yellow, seen from the front slightly above, no floor shadow
```

### sink — [PASS, 2nd roll] pixflux 96x96
```
soft pixel art, clean dark outline, children's game item, a single large white porcelain bathroom sink on a pedestal column with a small silver faucet, bright white with soft gray shading, big and centered filling the canvas, front view, no floor shadow
```
v1 [FAIL]: "...warm colors... a single white bathroom sink on a pedestal with a small silver faucet, front view, no floor shadow" — tiny pink cocktail-glass shape.

### toilet — [PASS, 3rd roll] **pixen** 96x96
```
a cute white cartoon toilet for a children's game, side view facing left, closed oval lid, round base, tall water tank behind with a small silver flush button, soft pixel art with a clean dark outline, the toilet is large and fills most of the canvas, transparent background, no floor shadow
```
(Came back with the lid open — reads perfectly, kept.) v1 [FAIL] pixflux "warm colors ... white toilet with the lid closed" — pink blob. v2 [FAIL] pixflux "large white porcelain toilet seen from the side..." — tiny, stuck in a corner.

## Kitchen set

### stove — [PASS, 1 roll] pixflux 96x96
```
soft pixel art, warm colors, clean dark outline, children's game item, a single cream kitchen stove with an oven door and four burner knobs, front view, no floor shadow
```

### fridge — [PASS, 1 roll + shadow scrub] pixflux 96x96
```
soft pixel art, warm colors, clean dark outline, children's game item, a single mint green retro refrigerator with a silver handle, front view, no floor shadow
```
Came back with a faint tan floor-shadow ellipse; removed in PIL (cleared the (199,188,161)-family pixels) rather than re-rolling.

### kitchen counter — [PASS, 2nd roll] pixflux 96x80
```
soft pixel art, warm colors, clean dark outline, children's game item, a single kitchen counter cabinet: a wide low wooden cabinet with two cupboard doors and a flat pale countertop slab on top, straight boxy furniture shape, front view, no floor shadow
```
v1 [FAIL] 96x96 "wooden kitchen counter with a light countertop and a lower cabinet door" — read as a treasure chest.

### cooking pot — [PASS, 1 roll] pixflux 64x64
```
soft pixel art, warm colors, clean dark outline, children's game item, a single silver cooking pot with two handles and a lid, front view, no floor shadow
```

## Construction set (all 1 roll, all PASS)

### hard hat — [PASS] pixflux 64x64
```
soft pixel art, warm colors, clean dark outline, children's game item, a single yellow construction hard hat, front view, no floor shadow
```

### traffic cone — [PASS] pixflux 64x64
```
soft pixel art, warm colors, clean dark outline, children's game item, a single orange traffic cone with a white reflective stripe, front view, no floor shadow
```

### toolbox — [PASS] pixflux 80x64
```
soft pixel art, warm colors, clean dark outline, children's game item, a single red metal toolbox with a handle and silver latch, front view, no floor shadow
```

### wooden crate — [PASS] pixflux 80x80
```
soft pixel art, warm colors, clean dark outline, children's game item, a single wooden shipping crate made of planks, front view, no floor shadow
```

### barrier — [PASS] pixflux 96x64
```
soft pixel art, warm colors, clean dark outline, children's game item, a single construction barrier board with yellow and black diagonal hazard stripes on two short legs, front view, no floor shadow
```

## Emotion marks (pixen 48x48)

### heart — [PASS, 1 roll]
```
bold pixel art emotion icon for a children's game, a single bright red heart with a clean dark outline, simple and readable at tiny size
```

### grumpy cloud — [PASS, 1 roll]
```
bold pixel art emotion icon for a children's game, a single grumpy gray storm cloud with an angry frowning face and a tiny yellow lightning bolt below, clean dark outline, simple and readable at tiny size
```

### sparkles — [PASS, 1 roll]
```
bold pixel art emotion icon for a children's game, three yellow four-pointed sparkle stars of different sizes, clean dark outline, simple and readable at tiny size
```

### sweat drop — [PASS, 1 roll]
```
bold pixel art emotion icon for a children's game, a single big light blue sweat drop, teardrop shape with a small white shine, clean dark outline, simple and readable at tiny size
```

### music note — [PASS, 1 roll]
```
bold pixel art emotion icon for a children's game, a single black eighth music note, clean dark outline, simple and readable at tiny size
```

### exclamation point — [PASS, 2nd roll]
```
bold pixel art emotion icon for a children's game, a red exclamation mark symbol: one thick vertical rounded bar with a separate round dot below it, bright red with a clean dark outline, simple flat shape readable at tiny size
```
v1 [FAIL]: "a single bright red exclamation point, thick and rounded..." — two stacked red blobs, unreadable.

## Environment starter set

Third-run session 2026-08-18. Sky pieces for kids' scenes: clouds, celestial, scenery
birds. Same pixflux house pattern; whites/greys drop "warm colors" per the porcelain
lesson. New lesson this run: **thin, pale or glowy shapes can come back 100% transparent**
under no_background (the background remover eats them) — "wispy... wide and thin" and "a
soft orange and pink glow" both returned empty PNGs; the fixes describe a SOLID body
("smooth white rounded bar shape", "solid orange half disc") and skip glow words. Second
new lesson: **mood words summon faces** — "grumpy shading" put a full kawaii face on the
storm cloud even with "no face" in the prompt; scenery clouds must stay mood-free and say
"no face no eyes no mouth".

### puffy cloud — [PASS, 1 roll] pixflux 96x64
```
soft pixel art, clean dark outline, children's game item, a single puffy white cumulus cloud, bright white with soft gray shading, big and centered filling the canvas, front view, no floor shadow
```

### wispy cloud — [PASS, 2nd roll + shadow-line scrub] pixflux 96x48
```
soft pixel art, clean dark outline, children's game item, a single long flat stretched cloud, a smooth white rounded bar shape with soft gray shading underneath, big and centered filling the canvas, front view, no floor shadow
```
Baked grey floor-shadow line under the base ((115,115,116) row) cleared in PIL.
v1 [FAIL]: "...a single flat stretched wispy white cloud, wide and thin, bright white with soft gray shading..." — came back COMPLETELY EMPTY (0 opaque pixels); the background remover ate the thin pale shape.

### storm cloud — [PASS, 2nd roll + shadow scrub] pixflux 96x64
```
soft pixel art, clean dark outline, children's game item, a single plain dark gray rain storm cloud, background scenery with no face no eyes no mouth, lumpy round top and darker gray flat base, big and centered filling the canvas, front view, no floor shadow
```
Baked grey drop-shadow ellipse ((98,98,103)/(93,93,99)) cleared in PIL.
v1 [FAIL]: "...gray storm cloud with darker gray grumpy shading underneath, no face..." — full kawaii face (eyes, blush, smile). "grumpy" summons the emotion-mark style even with "no face".

### sun — [PASS, 1 roll] pixflux 64x64
```
soft pixel art, warm colors, clean dark outline, children's game item, a single round warm yellow sun with short triangular rays, front view, no floor shadow
```

### setting sun — [PASS, 2nd roll + horizon-stub scrub] pixflux 96x64
```
soft pixel art, warm colors, clean dark outline, children's game item, the top half of a large round setting sun, a solid orange half disc with pink shading near the flat straight bottom edge, big and centered filling the canvas, front view, no floor shadow
```
A thin baked horizon line stuck out both sides of the disc on the bottom row; cleared in PIL.
v1 [FAIL]: "...the top half of an orange sun disc with a soft orange and pink glow around it, flat straight bottom edge..." — came back COMPLETELY EMPTY (0 opaque pixels); the glow took the whole disc with it in background removal.

### moon — [PASS, 1 roll] pixflux 64x64
```
soft pixel art, clean dark outline, children's game item, a single round full moon, pale gray with soft gray craters, big and centered filling the canvas, front view, no floor shadow
```

### crescent moon — [PASS, 1 roll] pixflux 64x64
```
soft pixel art, warm colors, clean dark outline, children's game item, a single soft yellow crescent moon, front view, no floor shadow
```

### star — [PASS, 2nd roll + satellite scrub] **pixen** 32x32
```
bold pixel art for a children's game, a single yellow four-pointed sparkle star, a diamond twinkle with one point up one point down one point left one point right, clean dark outline, simple and readable at tiny size
```
Came back as a bold 8-point twinkle with two tiny satellite sparkles; satellites cleared in PIL (connected-component keep-largest), reads clearly at 16px.
v1 [FAIL]: "...a single yellow four-pointed twinkle star..." — classic five-pointed star, not a twinkle.

### star cluster — [PASS, 1 roll] pixen 48x48
```
bold pixel art for a children's game, a loose sprinkle of four tiny yellow four-pointed sparkle stars of different sizes, clean dark outline, simple and readable at tiny size
```
(Delivered five stars of different sizes — inside the 3-5 spec, kept.)

### shooting star — [PASS, 2nd roll] pixen 64x32
```
bold pixel art for a children's game, a single yellow four-pointed shooting star with a long yellow motion tail streaking behind it to the left, plain star with no face no eyes, clean dark outline, simple and readable at tiny size
```
(Star is five-pointed; the order only pins 4-point for the small star, kept.)
v1 [FAIL]: same prompt without "plain star with no face no eyes" — tiny face on the star.

### flying bird — [PASS, 2nd roll] pixflux 48x48
```
soft pixel art, clean dark outline, children's game item, a single dark gray seagull silhouette soaring with wings spread wide and flat, plain solid silhouette with no eyes and no face, side view, no floor shadow
```
v1 [FAIL]: "...bird silhouette gliding with long outstretched gull wings, no face..." — flapping pigeon with a red eye dot.

### bird flock — [PASS, 4th roll + one-bird scrub] pixflux 96x48
```
soft pixel art, clean dark outline, children's game item, five tiny dark birds flying far away in the sky, each one drawn as a simple curved letter m of two wing arcs like distant seagulls in a landscape painting, spread out across the whole canvas from left edge to right edge at different heights, no eyes no beaks, side view, no floor shadow
```
Delivered SIX birds; one removed in PIL (connected components) to land inside the 4-5 spec.
v1 [FAIL]: "a loose flock of five tiny dark distant bird silhouettes shaped like flat letter m, scattered across the canvas..." — a size-ordered LINE of round beaked birds, like an animation strip.
v2 [FAIL]: "...each one a simple wide checkmark shape of two curved wings, scattered at different heights..." — good scatter but two of five marks were asterisks that read as sparkles, not birds.
v3 [FAIL]: the v4 prompt without "spread out across the whole canvas from left edge to right edge" — four good birds but in a flat centered row using a third of the canvas.
