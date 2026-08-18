# Rooms — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
Status marks: [PASS] verified good, [FAIL] rejected (kept for the record).
Generator: create_image_pixflux, 320x240, no_background=false. First-run session 2026-08-17.

Lesson of the session: **naming the room type summons its furniture.** "bathroom" baked in
tubs/sinks/toilets through four attempts (even with "no fixtures, nothing installed yet");
the pass came from describing only the materials and never saying the word. "kitchen"
needed the "freshly built, before any appliances have been installed" framing plus an
explicit tile-band cue. img2img over farmhouse.png at strength 100 just clones farmhouse;
strength 50 loses the material words too — text-only won every room.

## construction — [PASS] (installed as room 'construction', 1 roll)

outline "selective outline", shading "basic shading", detail "medium detail":

```
soft pixel art children's game room background, flat front view of a bare factory building-site interior, completely empty room, gray concrete block walls, one high framed window on the back wall showing blue sky, yellow and black hazard stripe accent band along the base of the wall, smooth concrete floor along the bottom, warm cheerful lighting, no crates, no machines, no tools, no objects at all
```

## kitchen-bare — [PASS] (installed as room 'kitchen-bare', 4th roll)

seed 7, outline "selective outline", shading "basic shading", detail "medium detail":

```
soft pixel art children's game room background, a freshly built kitchen room before any appliances have been installed, completely empty, warm butter-yellow walls with a band of white square tiles along the lower half, one bright framed window on the back wall showing blue sky and clouds, warm honey-brown wooden plank floor along the bottom, cozy cheerful lighting, nothing installed yet, no stove, no fridge, no counter, no furniture, no objects
```

Failed kitchen attempts (do not reuse): v1 text "flat front view of a bare kitchen interior
... no furniture, no stove, no fridge, no counter, no objects at all" — stove + counter
baked in. v2 img2img farmhouse strength 100 — farmhouse clone. v3 img2img v2 strength 50 —
still a farmhouse clone, no kitchen cue.

## bathroom-bare — [PASS] (installed as room 'bathroom-bare', 5th roll)

The winner never says "bathroom". outline "selective outline", shading "basic shading":

```
soft pixel art children's game room background, a completely empty room with pale blue and white square ceramic tile walls, a visible tile grid, one bright framed window on the back wall showing blue sky and clouds, a shiny light blue ceramic tile floor along the bottom, bright cheerful lighting, the room is bare and contains nothing at all, no furniture, no objects
```

Failed bathroom attempts (do not reuse): v1 "flat front view of a bare bathroom interior
... no furniture, no bathtub, no sink, no toilet, no objects at all" — tub + shower
curtain baked in. v2 img2img farmhouse strength 100 — farmhouse clone, no tile. v3 img2img
strength 50 — speckled cream walls, no tile grid. v4 "freshly built bathroom ... nothing
installed yet, no fixtures" (seed 7) — sink, tub AND toilet baked in.

Owner verdict 2026-08-17 on bathroom-bare: "didn't quite land" — too busy/clinical. The
calmer retake is 'washroom' below.

## Second-run session 2026-08-17 — five bare rooms, 7 rolls

All create_image_pixflux 320x240, no_background=false, outline "selective outline",
shading "basic shading", detail "medium detail". Text-only, no img2img, no seed.
Session lesson: for OUTDOOR scenes the bottom walk band needs its own explicit clause —
"a plain smooth flat ... strip along the bottom with no flowers and no tall grass" —
or the model decorates the y≈200–240 band with a flower/tall-grass hedge (meadow v1).

## washroom — [PASS] (installed as room 'washroom', 1 roll) — the calm bathroom retake

Never says the room's name; kitchen-bare's tile-band structure in warm tones:

```
soft pixel art children's game room background, a completely empty room with soft warm cream walls and a band of pale peach square ceramic tiles along the lower half, one bright framed window on the back wall showing blue sky and clouds, a warm honey-toned ceramic tile floor along the bottom, cozy gentle lighting, the room is bare and contains nothing at all, no furniture, no objects
```

## worksite — [PASS] (installed as room 'worksite', 1 roll) — outdoor construction

Never says "construction"; materials + a distant girder silhouette + hazard stripes:

```
soft pixel art children's game outdoor background, flat front view of a wide open flat dirt and gravel yard under a bright blue sky with white clouds, a distant gray steel girder frame silhouette far away on the horizon, a yellow and black hazard stripe accent band along a low gray concrete barrier at the back of the yard, smooth packed light brown dirt ground along the bottom, warm cheerful lighting, no vehicles, no machines, no tools, no crates, no objects at all
```

## meadow — [PASS] (installed as room 'meadow', 2nd roll)

```
soft pixel art children's game outdoor background, flat front view of a wide open rolling green grass field under a bright blue sky with white clouds, a few small leafy trees far away at the left and right edges, tiny colorful flowers scattered only in the far distance near the horizon, a big clear open grass area in the middle, a plain smooth flat green grass strip along the bottom with no flowers and no tall grass, warm cheerful lighting, no animals, no buildings, no objects at all
```

Failed meadow v1 [FAIL] (do not reuse): "... tiny colorful flowers scattered only near the
edges ... flat green grass ground along the bottom ..." — a dense flower and tall-grass
hedge ran across the ENTIRE bottom edge, right through the walk band. The fix that won:
push flowers "only in the far distance near the horizon" and give the bottom strip its own
"plain smooth flat ... no flowers and no tall grass" clause.

## clearing — [PASS] (installed as room 'clearing', 1 roll)

```
soft pixel art children's game outdoor background, flat front view of a wide open grassy glade surrounded by tall leafy green trees at the left and right edges and far in the background, a bright blue sky with white clouds above the treetops, warm sunlight falling on the middle of the glade, a big clear open grass area in the middle, a plain smooth flat green grass strip along the bottom with no flowers and no tall grass, warm cheerful lighting, no animals, no buildings, no objects at all
```

(Thin dark grass fringe on the very bottom edge — accepted, it reads as a vignette frame
and the sunlit oval middle is the walkable area.)

## beach — [PASS] (installed as room 'beach', 1 roll)

Never says "beach" — sandy shore + water at the horizon:

```
soft pixel art children's game outdoor background, flat front view of a wide open sandy shore under a bright blue sky with white clouds, calm blue water with gentle white waves far away along the horizon, a few small tufts of dune grass only at the left and right edges, a big clear open area of smooth pale golden sand in the middle, a plain smooth flat sand strip along the bottom with no shells and no tall grass, warm cheerful lighting, no people, no animals, no boats, no objects at all
```

## backyard-night — [FAIL / not chosen] (generated, not installed)

Came out clean but weakest of the D candidates: the fence + bush midground band is
muddled and the whole scene reads flat-dark next to the clearing. Kept for the record:

```
soft pixel art children's game outdoor background, flat front view of a calm quiet night scene, deep blue starry sky with a bright crescent moon and a few small soft clouds, a low wooden fence far away at the back, a few small round bushes only at the left and right edges, a big clear open grass area in the middle, a plain smooth flat dark green grass strip along the bottom with no flowers and no tall grass, soft cozy moonlight, no people, no animals, no buildings, no objects at all
```

## station — [PASS] (installed as room 'station', 1 roll) — the lofi train stop

Owner's ask: "a train station for the lofi vibes." Golden-hour sky + string lights +
lamppost + rails behind the platform = the vibe, first roll. The distant train silhouette
in the prompt did NOT render — fine; a little train can be a placeable ITEM instead.
Note the lamppost bakes in on the right edge; a keep-out or prop-home can respect it.

```
soft pixel art children's game outdoor background, flat front view of a wide open empty paved platform beside a railway line, warm golden sunset sky with soft orange and pink clouds, a distant cozy little train silhouette far away on the rails at the back, a string of small warm glowing round lights hanging along the top of the back wall, a big clear open area of smooth pale gray pavement in the middle, a plain smooth flat pavement strip along the bottom with no benches and no signs, calm cozy golden-hour lighting, no people, no luggage, no benches, no clutter, no objects at all
```
