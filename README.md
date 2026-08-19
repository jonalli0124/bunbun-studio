# Bunbun Studio

A virtual pet for a $20 ESP32-S3 touchscreen — and the browser tools that let a
seven-year-old build its world: draw props, author animations, assemble rooms, and
carry it all to the device as a single file.

The pet has real needs on real clocks. The kids furnish its world. The device honors
what they build, exactly — the **scene assembler is the spec**.

## What's in the box

- `firmware/` layout (this repo root) — ESP-IDF firmware: AirPlay 2 speaker + the pet.
  No cloud, no telemetry, no auto-updates: updates are USB or the manual web upload.
- `tools/` — the Scene Assembler and Animation Creation web pages (pure client-side),
  plus the pipeline scripts. Open `tools/build/` output in any browser or host it
  anywhere static.
- `tools/device_import.html` — the on-device import page: a kid drops a `.bunbun`
  package on `http://<device-ip>/build` and the device installs it, checking the pet
  before and after.
- `assets/` — bare rooms, props, lights, emotion marks; `assets/characters/capybara`
  is a complete generated species pack. `PROMPTS.md` files log every winning
  generation prompt verbatim.
- `pak-factory/` — the asset tree + converter that builds the device's art pak.
- `docs/` — the illustrated kid guide ("Bunbun and Me", HTML + PDF) and the
  grown-up workflow guide.
- `releases/` — a prebuilt image for the Freenove ESP32-S3 CYD.

## Quickstart

1. Flash `releases/bunbun-studio-esp32s3.bin` (USB, esptool or your favorite flasher).
2. Join the device's setup hotspot, give it your WiFi.
3. Open the tools on the project site (GitHub Pages) — or locally: `py tools/build.py`
   then open `tools/build/scene_tool.html`. Build rooms (toggle "this room is" for the
   kitchen / bathroom / work room), press **save a bunbun package** — one timestamped
   file carries the whole world, art and walk clips included.
4. Visit `http://<device-ip>/build`, drop the file, press the button. Done. A package
   replaces all custom animations; the pet is checked before and after.
5. Want a different animal? The site's **animal shelf** has a `.bunbun` per species —
   import it the same way; the pet's age and stats survive the body swap.

## The rules that make it work

- **The assembler is the spec.** The device must match the tool; fixes go in firmware.
- **The export is canon.** A kid's art replaces same-name art on the device.
- **Rooms are bare.** Furniture is the kids' job — that's the whole scene contract.
- **The pet is never reset.** Updates carry its age and history forward, always.
