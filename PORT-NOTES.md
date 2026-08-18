# bunbun ↔ AirPlay 2 port — working notes

Merging bunbun (the pet) **into** this AirPlay 2 app, rather than the reverse: this app already
owns WiFi, provisioning, settings, the web UI, PTP sync and the audio pipeline, so bunbun becomes
the display/game layer on top.

Branch: `bunbun-integration`. Board: Freenove ESP32-S3 CYD (FNK0104AB), 16MB flash, 8MB PSRAM.

## Build

```
build-freenove.bat            build
build-freenove.bat flash      flash (PORT=COM24 by default)
build-freenove.bat menuconfig
```

**Use idf.py, not PlatformIO.** Once Arduino-as-a-component pulls in Espressif's telemetry stack,
PlatformIO mangles the path for components that embed binary data — it emits
`.pio/build/<env>/.pio/build/<env>/https_server.crt.S` and fails on a file that is present.
Upstream CI uses idf.py and the repo already ships one PlatformIO workaround.

## Done

* **Board support** — `sdkconfig.defaults.freenove-s3` + `user_platformio.ini`. ES8311 on I2S
  (MCLK 4, BCK 5, WS 7, DO 8), codec I2C on the shared touch/RTC bus (SDA 16, SCL 15), amp enable
  GPIO1.
* **AirPlay 2 validated on this hardware** — native in Control Centre, PTP multi-room sync holds
  against a real AirPlay 2 speaker, DMAP metadata arrives (Artist/Title/Album), 222KB heap free.
* **Arduino core 3.3.11 as an IDF component**, resolving against IDF 5.5. This is what lets
  bunbun's ~3,600 lines come across intact instead of being rewritten against raw IDF.
* **TFT_eSPI as a component**, configured for the ILI9341.

## Traps already paid for

* **`sdkconfig.defaults` only SEEDS a fresh `sdkconfig`.** Editing defaults with a generated
  config present changes nothing. Delete `sdkconfig` to re-apply. Cost hours on a volume constant
  the build was never reading.
* **`idf.py` silently defaults to plain `esp32`** — builds a working-looking image for the wrong
  chip *and* skips every component rule gated on `target == esp32s3`, i.e. Arduino. Always pass
  `-DIDF_TARGET=esp32s3` (the wrapper does).
* **PlatformIO's IDF copy ships `export.bat`/`install.bat` with Unix line endings**, so every
  `goto` fails with "cannot find the batch label specified". Converted to CRLF in place.
* **IDF's installer refuses to run from inside a virtualenv.** Use `~/.platformio/python3`
  (the real interpreter), not `~/.platformio/penv`.
* **Arduino's `WiFi.h` shadows this project's `main/network/wifi.h`** on a case-insensitive
  filesystem, feeding C++ headers to a C compiler. Fixed by disabling Arduino's Network stack —
  correct anyway, since the IDF app owns WiFi.
* **Arduino selective compilation is OPT-OUT.** Most libraries are `default y`; listing what you
  want adds nothing. Disable explicitly.
* **`CONFIG_TFT_DC` had `range -1 31`** (an ESP32-classic assumption `TFT_CS` does not make), so
  GPIO46 was silently clamped to -1 and tripped TFT_eSPI's own "Invalid Data/Command pin" error.
  Patched in `components/tft_espi/Kconfig`.
* **TFT_eSPI as an IDF component reads Kconfig (`CONFIG_TFT_*`), NOT the `-D` flags** that
  platformio.ini uses. The `-D` form compiles and then fails its own guards.
* **Backlight**: `CONFIG_ENABLE_BL` is deliberately OFF. bunbun drives GPIO45 through LEDC for its
  dimming curve; letting TFT_eSPI claim the pin is the documented trap that pulls it out of the
  LEDC matrix and pins the panel at full brightness.

## Remaining

1. bunbun's sources (`main.cpp`, `ui.h`, `game.h`, `sfx.h`, `beat.h`) as a component; `setup()`/
   `loop()` become a task started from this app's `app_main`.
2. Assets partition. `components/boards/partitions-16m.csv` has room — add a `bunbun_assets`
   entry and flash `bunbun.pak` (~1.14MB) to it.
3. Strip bunbun's `net.h`, its AirPlay 1 glue and its captive portal — all owned by this app now.
4. Point the now-playing ticker at this app's DMAP metadata (which works, unlike RAOP's).
5. Re-point dance-mode beat detection at this app's audio output instead of `audio_process_i2s`.
6. **Scope cut**: drop SD-card MP3 playback. It requires ESP32-audioI2S, which fights this app's
   pipeline for I2S ownership — the single hardest remaining piece, and AirPlay makes it largely
   redundant.
7. Re-verify PTP sync after the `CONFIG_FREERTOS_HZ=1000` change Arduino requires (10x tick).

## Performance debugging (2026-08-03 evening) — all found by measurement

The loop prints `perf: iterMax/drawMax/i2cMax` every 2s. Every stall below was invisible until
one of those three named its owner. In order of discovery:

| symptom | cause | fix |
|---|---|---|
| loop locks at exactly 10Hz | draw gate stamped BEFORE the draw: one >100ms draw makes the next due instantly, back-to-back forever | stamp `lastDraw` AFTER the draw |
| draws 15ms -> 70-100ms while streaming | audio jitter buffer evicting the PSRAM sprite from a 32KB dcache | `CONFIG_ESP32S3_DATA_CACHE_64KB` |
| 250-380ms iter spikes | per-frame `esp_partition_read` queueing behind the NVS/SPIFFS/WiFi flash lock | mmap the pak; reads become cached memcpy |
| **12.2-SECOND loop pass** | Serial (USB-JTAG) writes block when no PC drains the console — froze whenever the port closed, healed whenever a capture opened it | `Serial.setTxTimeoutMs(0)` |
| "stalls in dance mode" only | disco lights: ~24,000 readPixel/drawPixel per frame on a PSRAM sprite under audio bus load; NEVER measured because captures ran with dance off | direct framebuffer access via `getPointer()` (sprite stores 16bpp BYTE-SWAPPED — bracket arithmetic with swaps) |
| audio err -4..-34ms, constant drains | bunbun at task priority 5 stealing time from playout | priority 2; audio timing outranks frame rate |

Meta-lesson of the evening: three of these six only reproduced when nobody was measuring.
The serial one literally healed on observation. Instrument with counters that survive to the
next connection; never trust a symptom that changes when you attach the debugger.

## Board inventory (2026-08-03)

| board | state |
|---|---|
| fresh (MAC ..D424) | full port, touch perfect, all fixes — the reference unit |
| original main (..D0C4) | FIXED — the touch panel's flex ribbon had unseated (codec on the same bus kept ACKing; far devices dropped = break past the codec junction). Reseat revived 0x38, ok=10049/fail=0. Unit three. |
| spare (..438C) | WiFi TX dead (proven by association test), touch fine — return pile; useful as A/B testbed |
| original pet board | untouched standalone Arduino bunbun, the real pet's save intact |

Default AirPlay name is now `bunbun-XXXX` (eFuse MAC) so multiple units are distinguishable in
Control Centre from the first advertisement. Web-UI name overrides.

## Overnight build (2026-08-04, ~02:00) — layer cache, SFX, SD playback

All three land through ONE architectural idea: the host's playback loop is the single mixing
point. Its receiver branch = AirPlay; its former silence branch = bunbun's slot (SD ring, then
SFX mix over whatever resulted). Precedence needs no state machine.

* **Layer cache**: dance frames now copy a cached room base (refreshed 4Hz / on dim change) and
  redraw only character+disco. Verified headless via the new serial `d` toggle: drawMax steady
  27ms (= the base pass), where full composes ran 60-90ms jittery. STREAMING dance verification
  still needs a phone: expect the 60ms gate to hold (~16.7fps locked).
* **SFX/rain**: sfxMixInto() called by the host on every block, after the beat tap. Bleeps work
  in silence because the loop never stops writing frames.
* **SD playback** (components/bunbun/sdplayer.h): helix -> PSRAM ring -> silence branch. One
  task owns the card (hot-swap = read-failure + re-probe, the standalone rule). 44.1kHz only —
  the host's I2S must stay AirPlay-clocked, so other rates are skipped with a ticker message.
  UNVERIFIED with a real card (none inserted overnight): needs music on a card + ears.

Morning checklist:
1. Insert the music SD card into a unit -> tracks scan, plays when nothing streams, pauses the
   instant AirPlay starts, resumes after. TRACKS panel selects; SND music level 0 stops it.
2. Stream + dance mode -> confirm the 60ms gate holds (perf: line over serial, or by eye).
3. Bleeps/rain audible again (tap a menu button; wait for a shower).
4. The D0C4 unit (was COM24) missed the overnight flash — one `build-freenove.bat flash` when
   plugged in. The other two are current.
5. Pre-existing: one unit idles at ~21Hz loop where its twin runs ~100Hz — real but cosmetic,
   still undiagnosed.

## Shipped (2026-08-04, ~03:10) — the streaming-dance saga, resolved

Final measured state, streaming + dance: **20-23fps steady, drawMax ~19ms, discoMax 6-9ms**,
no tearing, no audio clipping. "Pretty good but not perfect" — shipped on those numbers.

What the day taught, in the order the instruments taught it:

1. **The overnight layer cache was retired by measurement**: it optimized the flash-lock
   bottleneck that mmap had already removed, and its full-frame PSRAM copy cost more than the
   compose it saved. Replaced by discoBandStage: lights/ball applied per-band in internal SRAM.
2. **Two audio bugs only an EAR could catch**: (a) AirPlay's buffer runs dry a frame at a time
   routinely, and the SD silence-branch was splicing 8ms MP3 shards into streamed music —
   fixed with 2s idle hysteresis before local audio may engage; (b) rain's duck+softclip
   compressed full-scale streamed music — rain no longer mixes over a live stream.
3. **The real fps villain was CORE PLACEMENT**: the host pins playback (pri 7) and RTP (pri 8)
   to core 1, where bunbun sat at pri 2 — froze up to 407ms mid-draw. Caught by per-stage
   attribution (discoMax reading wall-time >> arithmetic). bunbun lives on core 0 with WiFi
   now; SD decoder on core 1 fills audio's gaps.
4. **Per-band time reads tear the frame**: beatPulse() evaluated per band let a flash edge land
   mid-frame — top of screen lit, bottom not. Snapshot once per frame.
5. Ball rotation constant by user request; the beat lives in the flicker.
6. Real WiFi off/on via SND button (wifi_user_set suppresses all host auto-reconnect paths).

SD player: mount/scan/decode/play verified live (including a real hot-swap: card pulled and
reinserted mid-session, remounted and resumed). 44.1kHz-only constraint stands.

Fleet at ship time: D0C4 + one sibling on this build; third unit needs one flash when next on
USB. Remaining polish, if daylight eyes still want it: occasional WiFi-burst hitches on core 0,
beat-lead tuning (BEAT_VIS_LEAD_MS=70), NTP-to-SNTP for self-setting clocks.

## Lessons of 2026-08-05 (the wish-storage day)

* **SPIFFS garbage-collects lazily, and the debt survives reboots.** A day of
  record/upload/delete churn (~10MB through the 4MB `storage` partition) left
  enough un-reclaimed pages that a 330KB fwrite came back short - sometimes
  84 bytes short of everything (the 128-byte husk uploads). `esp_spiffs_gc()`
  before any large write, and once at mount. Field signature: full-length
  "Wish saved ... SHORT WRITE" followed by a healthy upload of an empty shell.
* **Nothing big goes in event-handler locals.** A 628-byte `wifi_config_t` in
  one branch of the WIFI/IP event handler is charged to the sys_evt task's
  stack on EVERY event (the compiler merges branch frames) -> boot loop at the
  first event. Hand the work to a one-shot task; budget honestly: NVS
  string ops need ~2KB of stack on top of your locals (3072 was not enough
  for config + slot buffers + nvs_set_str; 4608 is).
* **A failing websocket handler must FAIL.** Returning ESP_OK for a dead
  socket keeps the httpd session alive and the handler gets re-invoked in a
  tight loop - the log viewer flooding the log with sock_err/masking warnings
  at exactly the moment you're field-debugging. ESP_FAIL = httpd hangs up.
* **Judge RAM floors on steady state, not one sample.** free_internal
  fluctuates a few hundred bytes with mDNS/httpd/TLS transients; a hard floor
  compared against a single read flaked 3x in one day (39123/39935/39879 vs
  40335 on identical code). The regression gate now takes the best of seven
  samples over its stability minute. The floor itself (40KB) never moves, and
  idle headroom is still only ~400 bytes - real reclamation is owed.
* **Never reboot a playing speaker.** Ship etiquette: publish the manifest
  immediately, then OTA each LAN unit only after 2 quiet minutes (the
  on-device self-updater already refuses while audio is live; the push path
  honors the same rule). `ota_when_quiet.py` in bunbun-nightly does this.
* **Honest failure messages are a feature.** "the wish got away" covered four
  unrelated failures and was wrong about half of them in the field; per-cause
  ticker lines (`wish_recorder_fail_reason()`) plus fs_total/fs_used in
  /api/system/info made the next diagnosis a screenshot instead of an
  afternoon.

## Lessons of the midnight USB session (W-020, 2026-08-06 00:00-00:30)

* **build-freenove.bat swallows build failures** - it exits 0 over a dead
  ninja. Trust only the output tail ("Hard resetting... Done") and a version
  probe of the device afterward. Cost one false victory before the rule.
* **esp_tinyusb cannot live in this tree**: its Kconfig (TINYUSB_MSC_ENABLED)
  wakes the Arduino core's dormant USBMSC.cpp, which expects a different
  tinyusb flavor. Three USB stacks, one build, no survivors.
* **The working pattern is usb_device_uac's**: a small component that injects
  its own tusb_config.h + descriptors INTO raw espressif__tinyusb
  (target_include_directories/target_sources on the tinyusb lib). Ours is
  components/usb_msc_bunbun - MSC-only personality, eight callbacks straight
  onto sdmmc_read/write_sectors, PHY->OTG, host-eject reboots to the pet.
* **The component graph trims aggressively**: tinyusb wasn't built until
  something REQUIRED it (S3-only list-append). And EXCLUDE_COMPONENTS loses
  to manager manifest deps - the yml is the truth; usb_device_uac had to
  leave the manifest, tinyusb adopted first-class.
* **Entry points must check availability**: the parked feature briefly
  shipped live buttons that rebooted into nothing. usb_msc_mode_available()
  gates every door now; a button that lies is worse than no button.
* extern to a C symbol from inside a C++ function mangles; declare in the
  extern "C" block.

## Lessons of the road session (2026-08-07, all delivered by chat-bin to a car)

* **Hotspot mode runs ~13KB below the home internal-RAM floor** (~27KB vs
  ~40KB ambient): classic-RAOP fallback keeps extra services resident. The
  regression gate only samples home mode - road margin is a fleet blind spot.
  Anything that worked at home and "mysteriously" fails on hotspot: check
  internal free FIRST.
* **mbedTLS was on internal heap** (DEFAULT_MEM_ALLOC): a GitHub TLS
  handshake wants ~50KB internal, so any on-device HTTPS under the 40KB
  floor could never pass. CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y moves the
  buffers to PSRAM. Set it in the GENERATED sdkconfig too - once that file
  exists, defaults-file edits silently do nothing.
* **A CHECK phase can run on a PSRAM stack; a FLASH phase cannot.**
  fw_update splits: check_task on a static 12KB SPIRAM stack (crt-bundle is
  cache-mapped rodata, not a spi_flash op), download_task internal-stacked
  and spawned only when a real update exists. NVS reads stay on the CALLER's
  stack - NVS is a flash op. Static task + static TCB so self-vTaskDelete
  frees nothing we still own.
* **The version belongs on the screen.** Five similar bin filenames on one
  phone burned an hour of debugging the WRONG BUILD. drawSetupPanel now
  shows esp_app_get_description()->version; every field session starts by
  reading it. Version bump + unique filename per delivered bin, always.
* **Restart confession works**: esp_reset_reason spoken on abnormal boots
  ("crash (panic)" / watchdog / power dip) diagnosed the field crash from
  the driver's seat. SW resets stay silent by design.
* **Correlation across bins is evidence**: plain 0.1.78 completed a check;
  b/c (panel-hold + version display) panic. The panel-hold (wakeMenu during
  CHECKING) is prime suspect; USB backtrace owed before any further guess.
  ELF for 0.1.78f preserved in the session scratchpad for addr2line.
* **Audio was network-gated since day one** (inherited AirPlay assumption):
  audio_output_init/start lived in start_airplay_services, which fires on
  network-CONNECT - AP/setup mode had no I2S at all, deterministically.
  Sound is a boot-time sense now: init in app_main AFTER bunbun_start()
  (the 86KB sprite must claim contiguous internal before the DMA ring
  fragments it). AirPlay keeps only the idempotent start for BT-resume.
* **Wish shelf UX shipped** (verified in the field same day): honest offline
  verdict ("it flies when wifi is back"), got-IP pokes the uploader
  (instant delivery on connect), "N unsent wishes" under the WISH button
  (blank at zero, 3s cached count - SPIFFS listing is not a per-frame op),
  shelf capped at 5 with the mic refusing until WiFi drains it.
* **Same-version-different-hash is not an update** (mv==rv rule in
  fw_update): without it the first working check re-flashed the approved
  bin over a newer dev build in an endless downgrade loop.

## The backtrace night (2026-08-07 evening, brother's house, serial.huhn.me)

* **The update-bar crash was a USE-AFTER-FREE all along** (fw_update.c
  downgrade-guard branches): cJSON_Delete(json) then ESP_LOG of
  jver->valuestring, which lives inside the freed tree. Freed-but-intact
  memory printed fine ("it worked"); a recycled block read NULL and ROM
  strlen panicked (LoadProhibited). Every theory of the day - stack size,
  TLS memory, panel-hold - was pattern-matching on allocator timing. The
  path only runs when running > manifest, first true the morning we
  hand-flashed past 0.1.51: why the fleet never saw it. Fixed (log first,
  free after) and field-confirmed: "up to date", no restart. LESSON: one
  browser serial capture (serial.huhn.me, 115200) + addr2line
  (C:\Espressif\tools\xtensa-esp-elf\...\xtensa-esp32s3-elf-addr2line)
  named it in minutes. Keep the per-build ELF; decode before theorizing.
* **The "low memory" refusals were a REAL, SEPARATE bug** (mbedTLS on
  internal heap) - two bugs shared one button. Both fixes stay.
* **Noisy-network internal sag (fleet finding, OPEN):** clean boot at a
  neighbor house = 32KB free / largest 23552 at AirPlay-ready, but by
  RECORD the session + a chatty network (WiFi driver dynamic RX buffers
  live in internal) diced it to ~16KB / largest 10240 < the receiver's
  12288 stack -> "Failed to create receiver task", AirPlay dead at that
  house while web-by-IP works. Home works (quiet network, ~40KB); hotspot
  works (quiet + classic mode). NOT fixable by layout shuffling: the
  boot-time-audio move made it worse, the static-stack rescue starved the
  rest (3KB free), both reverted same night. Bench candidates: PSRAM
  stack for the receiver (mDNS already runs from SPIRAM) with an A/B
  listening test, or trimming WiFi dynamic RX buffer count. Council item.
* **"Boot baseline" log at AirPlay-ready** (free internal + largest
  block) is the reference point that separated boot-history theories from
  session-time consumption. Cheap, permanent, already paid for itself.

## W-042 RESOLVED same night: receiver stack -> PSRAM = AirPlay anywhere

* Field-confirmed at the same house that exposed it ("we have music!"):
  the receiver task's 12KB stack now lives in PSRAM (static task, buffer
  allocated once, internal fallback kept), so busy-network internal sag
  can no longer block RECORD. Largest-block 10240 at session time is now
  irrelevant to the music path. Extended listening soak under dance/disco
  still owed as final certification; report any stutter against PSRAM bus
  contention before fleet-wide OTA.
* Process lesson with teeth (it bit AGAIN tonight): the first "j" bin was
  a DEAD BUILD - task_ret compile error, bat exited 0, the staged bin was
  silently the previous binary. Caught only by the version stamp refusing
  to change. Ship gate now: fresh bin mtime + unique git stamp + "Project
  build complete" in the tail. All three, every bin, no exceptions.
