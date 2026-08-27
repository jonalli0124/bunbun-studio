#pragma once
// One kid-facing sentence about what changed in this build, spoken on the
// ticker ONCE after the first boot of a new firmware version (the version
// string is compared against NVS "seenver").
//
// The nightly autoship pipeline overwrites this line when it ships wishes, so
// the morning after a wish comes true, bunbun says so itself. Keep it short —
// it is a ticker, not a changelog. Empty string = say nothing.
// W-054 phase 2 shipped: the NETWORKS picker on the gear shelf (plus the cat
// keeps her nap spot now, and music sounds right the moment he wakes). The
// picker is the family-visible half, so the ticker tells that one.
#define BUNBUN_WHATSNEW "bunbun can switch wifi from his gear shelf now - no more being stuck"
