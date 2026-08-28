#pragma once
// One kid-facing sentence about what changed in this build, spoken on the
// ticker ONCE after the first boot of a new firmware version (the version
// string is compared against NVS "seenver").
//
// The nightly autoship pipeline overwrites this line when it ships wishes, so
// the morning after a wish comes true, bunbun says so itself. Keep it short —
// it is a ticker, not a changelog. Empty string = say nothing.
// Council 2026-08-26, Motion 3(c): the device may not ship silent about its
// own growing up. Luna's three lines, verbatim, joined into the one ticker
// pass (longest whatsnew ever, on purpose - it is the biggest thing that has
// ever happened to this creature). The wifi picker line it replaces rides in
// the same build and the HOW-TO carries it.
#define BUNBUN_WHATSNEW "bunbun grows up now - babies, big kids, and all grown up ... tap bunbun's name to pick a new one, and choose cozy or brave ... treats from far away really count now"
