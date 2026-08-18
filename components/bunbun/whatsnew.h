#pragma once
// One kid-facing sentence about what changed in this build, spoken on the
// ticker ONCE after the first boot of a new firmware version (the version
// string is compared against NVS "seenver").
//
// The nightly autoship pipeline overwrites this line when it ships wishes, so
// the morning after a wish comes true, bunbun says so itself. Keep it short —
// it is a ticker, not a changelog. Empty string = say nothing.
// Menu redesign P3: the buttons at the bottom became four big doors, and the
// arrow keys went away entirely — the largest single change to how the device
// is held. The kids get told, in bunbun's own voice; say() swaps the sign-off
// for the pet's name.
#define BUNBUN_WHATSNEW "four big buttons at the bottom now - tap CARE for me, love bunbun"
