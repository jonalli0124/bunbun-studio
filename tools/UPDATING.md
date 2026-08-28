# Updating the tools

Two pages are published as links. Both are **built**, never edited in place.

| page | what it is | link |
|---|---|---|
| attach editor | put objects on a character, preview at real in-game scale | `claude.ai/code/artifact/06dd28fe-2956-470b-a31e-2f07ef044592` |
| Capybara Playhouse | scene builder for the kids | `claude.ai/code/artifact/f6479fd4-ff59-4071-b003-fb4eea466ecd` |

Both are private until shared from the page's own share menu.

---

## The loop

```
1. add or change art under assets/     (see assets/README.md for the three homes)
2. py tools/build.py
3. open tools/build/<page>.html        to check it
4. republish that same file            to keep the link
```

**Republish the same file path.** A different path creates a *new* artifact with a *new* link,
and the one you already sent to people stops updating. From a fresh conversation, pass the
existing URL explicitly so it updates in place rather than forking.

## What lives where

```
tools/
  src/attach_editor.html   EDIT THIS        the editor's real source
  src/scene_shell.html     EDIT THIS        the playhouse's real source
  build.py                 run this         regenerates data, inlines it, writes the pages
  mkdata.py                                 assets -> build/attach_data.json
  mkscene.py                                assets -> build/scene_data.js
  clean_sprite.py                           raw generator output -> clean frames
  repo_paths.py                             path resolution, so a clone works anywhere
  build/                   NOT COMMITTED    everything here is regenerated
```

`build/` holds the only copies anyone should open or publish. Each page carries all its art
inline, so it works from a double-click, a local server, or a share link, with no network
requests at all.

## Editing the source directly

`src/attach_editor.html` still runs unbuilt if you serve it next to an `attach_data.json` —
handy for a quick change without a rebuild. It prefers inline data when present and falls back
to fetching, so the same file covers both. The published page never fetches.

## Two things that will bite

**A stale build looks like art that did not change.** The pages embed their art; editing a PNG
does nothing until `build.py` runs. If a sprite looks wrong, rebuild before debugging.

**Downloads only work through the capability.** A plain download link is inert inside the
published viewer. Both pages route saves through `claude.use("downloads")` and fall back to a
link when running locally. If you add a new save button, route it the same way or it will
silently do nothing for anyone opening the link.

---

# Bringing a bunbun up to date

Written 2026-08-21, after doing it to D468 live — it was 55 versions behind, on
`0.1.236`, with no `/build` page at all. Asked twice in one evening, so it belongs here.

## 0. Read the pet FIRST

    http://bunbun-XXXX.local/api/system/info

Write down `pet_age_min`, `pet_phase`, `pet_stage`, `species_id`. **Those four must match
afterwards.** That is the whole contract; everything below is safe only because of it.
`XXXX` is the last four of the MAC. If mDNS will not resolve, find the IP on the router —
`arp -a` and probing `/api/system/info` across the subnet finds it in seconds.

## 1. It will NOT fix itself

**Nothing updates itself any more.** Devices used to pull `firmware/approved.json` overnight and
install anything newer; that machinery is gone from the shipping image (`BUNBUN_PUBLIC_BUILD`,
now the repo default). Verify it on any binary you are about to flash — config can drift, a
scan cannot:

    py -c "import io;b=io.open('build-freenove/airplay2-receiver.bin','rb').read();print(b.count(b'approved.json'))"

Zero is what you want. Every update below is deliberate and by hand, which is the point: no
device changes behaviour overnight while a child is asleep in the room with it.

## 2. Firmware

Build the fleet image, gate it, push it:

    set FLEET=1 && build-freenove.bat
    py tools/check_release.py --fleet
    py tools/push_firmware.py <ip>

**A bare `curl` to `/api/ota/update` no longer works, and should not.** Since 2026-08-27 a
fleet unit requires its own key in an `X-Bunbun-Key` header and answers 401 without it.
The reason is that `ota.c` never authenticated anything: it verifies a SHA-256 the
*uploader* appended, which proves the bytes arrived intact and nothing at all about who
sent them, so any device on the wifi could flash any bunbun (CI-2608). Keys are per unit
— one read out of a device's flash cannot flash any other — and live in
`C:/Users/Jon/bunbun-nightly/.otakeys`, outside the repo, next to `.ghtoken`. **Never
commit that file.**

`push_firmware.py` does the whole job and refuses to lie about it: it declines to push a
public image (that would cut the unit off from wifi updates for good) or one missing the
key guard, reports only a real 200 as success, insists the uptime actually reset, checks
`pet_id`/`born`/`stage`/`phase`/`species` are identical and the age did not go backwards,
provisions the key, and finally proves an anonymous push now gets a 401.

**Two things about keys.** Provisioning is trust-on-first-use: a freshly flashed unit is
unclaimed, and for those few seconds anyone on the wifi could claim it — so provision
immediately, which the script does. Once a key exists, changing it requires the old one,
and there is no clear operation; a unit whose key is genuinely lost is recovered over USB.

**The public image has no OTA endpoint at all** — not registered-and-refusing, absent,
along with `ota.c` itself. Public units are flashed with a USB cable. Build it by simply
*not* setting `FLEET`, and gate it with a bare `py tools/check_release.py`, which fails if
the string `/api/ota/update` appears in the binary at all.

**Check the silicon first if you have any doubt**: `free_heap` above ~6 MB means PSRAM,
i.e. an S3 board. A plain ESP32 CYD has no PSRAM and this image would brick it.

## 3. The build page

    py tools/deploy_builder.py --push <ip>

Old units serve an old `/build`, and it matters: D468 had a **486-byte stub** where the
25 KB importer should be, and 6D1C was serving a copy that rejected every modern package
with *"the package file looks cut short"*. The script re-fetches and compares, so you
know it landed.

## 4. The world

Export a `.bunbun` from the Assembler and give it to `http://<ip>/build`. That carries
the scene **and** the animal's kit, so it also cures stale or mis-scaled species art.

Note this restarts the device on purpose — `/api/ota/assets` writes the pak and calls
`esp_restart()`. A `reset_reason 3` (software) right after an install is the design, not a
crash.

## Backup before you write

    py tools/package_from_device.py bunbun-XXXX.local backup.bunbun

Rebuilds a `.bunbun` from the five scenes in SPIFFS plus the art they name. Worth doing
before step 4 on any unit that matters. That write erases as much of the art partition as the
upload is long, in one blocking call, and the renderer stand-down (`fw_assets_writing()`) is
**never raised on this path in any build** - `s_art_writing` is set only by the fleet art
fetcher, so a `/build` install has always drawn from flash that is being erased underneath it.

## The visiting cat

If she never appears, it is almost certainly not her timer — **her art is missing.** The nine
clips live in the shipped pak; no world package carries them, and installing a world REPLACES the
pak. So a device brought up the way this page describes has no cat art at all, and `drawCat()`
fails and returns silently: she walks in, sits, naps and leaves completely invisible.

Check:

    curl -s http://<ip>/api/ota/assets --output pak.bin
    py -c "import io;print(io.open('pak.bin','rb').read().count(b'cat-walk'))"

Zero means she is missing. Fix it by dropping `tools/build/visitors/cat.bunbun` on
`http://<ip>/build` — it adds her art and changes nothing else: not the world, not the
animal, not the pet. Build it with `py tools/make_visitors_package.py`.

## World package vs species package — they do different jobs

This cost an evening, so it is worth being explicit:

| | carries the animal's frames | sets the pet's species |
|---|---|---|
| **world** `.bunbun` from the Assembler | yes | **no** (until 0.1.291) |
| **species** `.bunbun` from `tools/build/species/` | yes, that animal only | yes |

Before 0.1.291 a pet could stand in a penguin world, with 90 penguin frames in the pak,
and still draw as the **base bunny** for anything the world had no clip of — because
`pa()` only prefixes with the species when `species_idx != 0`. It showed up as a
one-second bunny every fifteen seconds: the spontaneous joy hop, which no world authors.

From 0.1.291 the pet **adopts** the animal his world declares, provided the pak really
carries it. If you are on older firmware, install the matching species package.

## Setting one up to give away

Do it AT HOME, on your own network, before it leaves the house:

1. Flash it from the flasher (full erase, then the starter package).
2. Let it join wifi (Improv over the cable, or the AP portal).
3. **Add it:**

       py tools/wish_units.py --add <its ip>

`--add` is `--arm` plus the step everybody forgets. It asks the unit its own name and MAC,
arms the wish uploader, and writes it into `tools/fleet-units.json` so `--check` asks after
it from then on.

4. **Confirm the purr by hand before it leaves.** Pet it and feel the buzz with your own
   fingers. The software defaults to believing a motor is present (W-110), so a unit with no
   motor - or a motor wire that lost the argument with the case - will happily promise a purr
   it cannot give, and the kid it goes to is the one who finds out.

**Why this step exists at all:** where wishes go is runtime configuration, deliberately - so
ONE image is safe to hand to anybody, and a unit only sends wishes when somebody arms it. The
cost is that a full erase, which is exactly what the flasher does, takes the destination with
it. An unarmed unit does not complain: it records to a shelf that holds SIX and then quietly
refuses, so a child keeps pressing the button and nobody finds out for weeks.

    py tools/wish_units.py --check

is the question "are all my units still listening?" as a command. Run it after any handover,
and after re-flashing anything.
