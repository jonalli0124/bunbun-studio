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
