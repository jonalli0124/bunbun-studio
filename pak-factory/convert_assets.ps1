# bunbun asset converter: PNG sprites -> packed RGB565 blob for the CYD.
#
# Why this shape:
#   * The board has 4MB flash and NO PSRAM, so the art has to be small and cheap to blit.
#   * The sprites are ~88% transparent (274,805 opaque pixels across 302 frames), so storing
#     full 96x96 rectangles wastes ~10x. Each frame is trimmed to its opaque bounding box and
#     then run-length encoded per row, which skips interior transparency too.
#   * RLE also makes blitting cheaper than a colour key: no per-pixel compare, just memcpy
#     of each run. There is no reserved "transparent colour", so no risk of a real pixel
#     matching the key.
#   * Rooms are fully opaque with only 62 unique colours, so they get a palette instead.
#
# Output: assets/bunbun.pak  +  src/assets_index.h
#
# Frame format (all little-endian):
#   u16 w, h          trimmed size
#   i16 offX, offY    where the trimmed box sits inside the original frame
#   u16 origW, origH  original frame size (needed to keep animations registered)
#   then h rows, each: u8 segCount, then segCount x { u8 skip, u8 len, len x u16 rgb565 }
#
# Run:  powershell -File tools\convert_assets.ps1

param(
  [string]$AssetDir = (Join-Path $PSScriptRoot "assets"),
  # Defaulted to the old 2432S028R project, which meant a converted pak landed in a directory
  # nothing flashes from any more. This copy belongs to the S3 build.
  [string]$OutDir   = $PSScriptRoot,
  # Character-pack species ids (CHARACTER-PACKS.md §5): each one owns the frames under
  # "<id>/...". Listed explicitly rather than sniffed, so a stray folder cannot quietly
  # become a species and start insetting the walkable area.
  [string[]]$SpeciesIds = @()
)

Add-Type -AssemblyName System.Drawing

$code = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public class BunbunPack {
  public class Frame {
    public string Name;
    public int OrigW, OrigH, W, H, OffX, OffY;
    public byte[] Data;
    public int OpaquePx;
  }

  // Panel correction, chosen by eye against the browser on the actual hardware.
  // The ILI9341 clone runs with inversion on and renders muted and warm, so the art is
  // pre-compensated here rather than at runtime: SAT pushes colours away from their luma,
  // and WHITE stretches levels so near-white reads as true white. bunbun's body is
  // 249,250,245 in the source (and his shading 209,215,211), so without the white lift he
  // renders as a dull grey rather than a white rabbit.
  //
  // Doing it here beats doing it on-device: this works on the full 8-bit values before
  // quantisation, whereas the device could only correct already-quantised RGB565.
  const double SAT = 1.30;
  const double WHITE = 246.0;

  static int Clamp255(double v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)(v + 0.5)); }

  static ushort To565(int r, int g, int b) {
    double luma = 0.299 * r + 0.587 * g + 0.114 * b;
    double w = 255.0 / WHITE;
    int nr = Clamp255((luma + (r - luma) * SAT) * w);
    int ng = Clamp255((luma + (g - luma) * SAT) * w);
    int nb = Clamp255((luma + (b - luma) * SAT) * w);
    // round to the nearest representable level rather than truncating the low bits,
    // which halves the quantisation error on every channel
    int r5 = (nr * 31 + 127) / 255;
    int g6 = (ng * 63 + 127) / 255;
    int b5 = (nb * 31 + 127) / 255;
    return (ushort)((r5 << 11) | (g6 << 5) | b5);
  }

  // Returns ARGB bytes (BGRA order in memory) for the whole image.
  static byte[] GetPixels(Bitmap bmp, out int stride) {
    Rectangle rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
    BitmapData bd = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    stride = bd.Stride;
    byte[] buf = new byte[stride * bmp.Height];
    Marshal.Copy(bd.Scan0, buf, 0, buf.Length);
    bmp.UnlockBits(bd);
    return buf;
  }

  public static Frame Convert(string path, string name) {
    using (Bitmap bmp = new Bitmap(path)) {
      int stride;
      byte[] px = GetPixels(bmp, out stride);
      int W = bmp.Width, H = bmp.Height;

      // opaque bounding box (alpha >= 128 — these sprites have essentially binary alpha)
      int minX = W, minY = H, maxX = -1, maxY = -1, opaque = 0;
      for (int y = 0; y < H; y++) {
        int row = y * stride;
        for (int x = 0; x < W; x++) {
          if (px[row + x * 4 + 3] >= 128) {
            opaque++;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
          }
        }
      }
      Frame f = new Frame();
      f.Name = name; f.OrigW = W; f.OrigH = H; f.OpaquePx = opaque;
      if (maxX < 0) {   // fully transparent frame
        f.W = 0; f.H = 0; f.OffX = 0; f.OffY = 0; f.Data = new byte[0];
        return f;
      }
      f.OffX = minX; f.OffY = minY;
      f.W = maxX - minX + 1; f.H = maxY - minY + 1;

      MemoryStream ms = new MemoryStream();
      BinaryWriter bw = new BinaryWriter(ms);
      for (int y = 0; y < f.H; y++) {
        int row = (minY + y) * stride;
        // collect runs of opaque pixels on this row
        List<int> segStart = new List<int>();
        List<int> segLen = new List<int>();
        int x2 = 0;
        while (x2 < f.W) {
          while (x2 < f.W && px[row + (minX + x2) * 4 + 3] < 128) x2++;
          if (x2 >= f.W) break;
          int s = x2;
          while (x2 < f.W && px[row + (minX + x2) * 4 + 3] >= 128) x2++;
          // a run longer than 255 must be split, since len is a byte
          int len = x2 - s;
          while (len > 255) { segStart.Add(s); segLen.Add(255); s += 255; len -= 255; }
          segStart.Add(s); segLen.Add(len);
        }
        bw.Write((byte)segStart.Count);
        int prevEnd = 0;
        for (int i = 0; i < segStart.Count; i++) {
          int skip = segStart[i] - prevEnd;
          bw.Write((byte)skip);
          bw.Write((byte)segLen[i]);
          for (int k = 0; k < segLen[i]; k++) {
            int o = row + (minX + segStart[i] + k) * 4;
            bw.Write(To565(px[o + 2], px[o + 1], px[o + 0]));   // memory is BGRA
          }
          prevEnd = segStart[i] + segLen[i];
        }
      }
      bw.Flush();
      f.Data = ms.ToArray();
      return f;
    }
  }

  // The panel is used in PORTRAIT (240x320), so the 320x240 room has to come down to
  // 240x180 - exactly 4 source pixels to 3. Done here with area averaging rather than at
  // runtime with nearest-neighbour: same cost at render time, far less aliasing, and the
  // palette is rebuilt from the averaged result so no colours are invented.
  public const int ROOM_DST_W = 240, ROOM_DST_H = 180;

  static byte[] Downscale(byte[] px, int stride, int sw, int sh, int dw, int dh) {
    byte[] outp = new byte[dw * dh * 4];
    for (int dy = 0; dy < dh; dy++) {
      int sy0 = dy * sh / dh, sy1 = (dy + 1) * sh / dh;
      if (sy1 <= sy0) sy1 = sy0 + 1;
      for (int dx = 0; dx < dw; dx++) {
        int sx0 = dx * sw / dw, sx1 = (dx + 1) * sw / dw;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        // Point-sample the centre of the source block, NOT an average. Averaging blends a
        // new colour at every edge: it turned the nursery's 62 exact colours into 256+
        // (overflowing the palette) and softened every hard edge, which is the opposite of
        // what flat-shaded pixel art wants. Point sampling drops detail but keeps both the
        // palette and the crisp edges intact.
        int cy = (sy0 + sy1) / 2, cx = (sx0 + sx1) / 2;
        if (cy >= sh) cy = sh - 1;
        if (cx >= sw) cx = sw - 1;
        int o2 = cy * stride + cx * 4;
        int q = (dy * dw + dx) * 4;
        outp[q] = px[o2]; outp[q + 1] = px[o2 + 1];
        outp[q + 2] = px[o2 + 2]; outp[q + 3] = 255;
      }
    }
    return outp;
  }

  // Rooms: fully opaque, very few colours -> 8-bit palette + RGB565 palette table.
  public static void ConvertRoom(string path, out byte[] indices, out ushort[] palette,
                                 out int w, out int h) {
    using (Bitmap bmp = new Bitmap(path)) {
      int stride;
      byte[] px = GetPixels(bmp, out stride);
      px = Downscale(px, stride, bmp.Width, bmp.Height, ROOM_DST_W, ROOM_DST_H);
      stride = ROOM_DST_W * 4;
      w = ROOM_DST_W; h = ROOM_DST_H;
      Dictionary<ushort, int> map = new Dictionary<ushort, int>();
      List<ushort> pal = new List<ushort>();
      indices = new byte[w * h];
      for (int y = 0; y < h; y++) {
        int row = y * stride;
        for (int x = 0; x < w; x++) {
          int o = row + x * 4;
          ushort c = To565(px[o + 2], px[o + 1], px[o + 0]);
          int idx;
          if (!map.TryGetValue(c, out idx)) {
            if (pal.Count >= 256) {
              // fall back to nearest existing entry rather than failing outright
              int best = 0, bestD = int.MaxValue;
              int r1 = (c >> 11) & 0x1F, g1 = (c >> 5) & 0x3F, b1 = c & 0x1F;
              for (int i = 0; i < pal.Count; i++) {
                int r2 = (pal[i] >> 11) & 0x1F, g2 = (pal[i] >> 5) & 0x3F, b2 = pal[i] & 0x1F;
                int d = (r1-r2)*(r1-r2)*4 + (g1-g2)*(g1-g2) + (b1-b2)*(b1-b2)*4;
                if (d < bestD) { bestD = d; best = i; }
              }
              idx = best;
            } else {
              idx = pal.Count; pal.Add(c); map[c] = idx;
            }
          }
          indices[y * w + x] = (byte)idx;
        }
      }
      palette = pal.ToArray();
    }
  }
}
'@

Add-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing, System.Collections

# ---------------- gather assets ----------------
# Frames are named by their path relative to assets/, which is exactly how the web build
# refers to them (e.g. the loader's "walk-south/anim" + frame index -> walk-south/anim/3).
# This MUST recurse: the adult animations live one level down in an anim/ subfolder, and a
# non-recursive walk silently skipped every adult walk/work/bath/eat/sleep/jump/play frame.
$skipTop = @("_candidates", "rooms")
$frames = New-Object System.Collections.Generic.List[object]

$allDirs = @(Get-ChildItem $AssetDir -Directory -Recurse) | Where-Object {
  $rel = $_.FullName.Substring($AssetDir.Length + 1)
  $top = ($rel -split '[\\/]')[0]
  $skipTop -notcontains $top
}
foreach ($d in $allDirs) {
  $pngs = @(Get-ChildItem $d.FullName -Filter *.png)
  if ($pngs.Count -eq 0) { continue }
  $rel = $d.FullName.Substring($AssetDir.Length + 1).Replace('\', '/')
  # numeric frames sort numerically; anything else keeps its own name
  $numeric = @($pngs | Where-Object { $_.BaseName -match '^\d+$' } | Sort-Object { [int]$_.BaseName })
  $named   = @($pngs | Where-Object { $_.BaseName -notmatch '^\d+$' } | Sort-Object Name)
  foreach ($p in $numeric) { $frames.Add([BunbunPack]::Convert($p.FullName, "$rel/$($p.BaseName)")) }
  foreach ($p in $named)   { $frames.Add([BunbunPack]::Convert($p.FullName, "$rel/$($p.BaseName)")) }
}

Write-Output "converted $($frames.Count) sprite frames"
$tooLong = @($frames | Where-Object { $_.Name.Length -gt 31 })
if ($tooLong.Count) {
  Write-Output "WARNING: names too long for the 32-byte index field:"
  $tooLong | ForEach-Object { Write-Output "  $($_.Name)" }
}

# ---------------- write the pack ----------------
$assetsOut = Join-Path $OutDir "assets"
New-Item -ItemType Directory -Force $assetsOut | Out-Null
$pakPath = Join-Path $assetsOut "bunbun.pak"

# Index entry, 56 bytes:
#   name[32] offset:u32 size:u32 origW:u16 origH:u16 w:u16 h:u16 offX:i16 offY:i16
#   fmt:u8 (0 = RLE sprite, 1 = palettised room)  reserved[3]
# NOTE: this MUST match ENTRY_BYTES below and the struct in the firmware. An earlier version
# declared 48 while actually writing 52, so every recorded data offset was short by
# 4*count bytes and the whole blob would have decoded as garbage.
$ENTRY_BYTES = 56
$HEADER_BYTES = 8

function Write-Entry($bw, $name, $offset, $size, $origW, $origH, $w, $h, $offX, $offY, $fmt) {
  $nameBytes = New-Object byte[] 32
  $src = [System.Text.Encoding]::ASCII.GetBytes($name)
  [Array]::Copy($src, $nameBytes, [Math]::Min($src.Length, 31))
  $bw.Write($nameBytes)
  $bw.Write([uint32]$offset)
  $bw.Write([uint32]$size)
  $bw.Write([uint16]$origW); $bw.Write([uint16]$origH)
  $bw.Write([uint16]$w);     $bw.Write([uint16]$h)
  $bw.Write([int16]$offX);   $bw.Write([int16]$offY)
  $bw.Write([byte]$fmt)
  $bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([byte]0)
}

# rooms are packed into the same blob so there is a single artefact to flash
$roomDir = Join-Path $AssetDir "rooms"
$roomFiles = @(Get-ChildItem $roomDir -Filter *.png | Where-Object { $_.Name -notmatch 'backup|v1|v2' })
$roomBlobs = @()
foreach ($r in $roomFiles) {
  $indices = $null; $palette = $null; $w = 0; $h = 0
  [BunbunPack]::ConvertRoom($r.FullName, [ref]$indices, [ref]$palette, [ref]$w, [ref]$h)
  $ms = New-Object System.IO.MemoryStream
  $mw = New-Object System.IO.BinaryWriter($ms)
  $mw.Write([uint16]$palette.Length)
  foreach ($c in $palette) { $mw.Write([uint16]$c) }
  $mw.Write($indices)
  $mw.Flush()
  $roomBlobs += [PSCustomObject]@{ Name = "rooms/$($r.BaseName)"; Data = $ms.ToArray(); W=$w; H=$h; Colours=$palette.Length }
  Write-Output ("  room {0}: {1}x{2}, {3} colours" -f $r.BaseName, $w, $h, $palette.Length)
}

$totalEntries = $frames.Count + $roomBlobs.Count
$running = $HEADER_BYTES + ($totalEntries * $ENTRY_BYTES)

$fs = [System.IO.File]::Create($pakPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([byte[]][char[]]"BUNP")
$bw.Write([uint16]2)
$bw.Write([uint16]$totalEntries)

foreach ($f in $frames) {
  Write-Entry $bw $f.Name $running $f.Data.Length $f.OrigW $f.OrigH $f.W $f.H $f.OffX $f.OffY 0
  $running += $f.Data.Length
}
foreach ($r in $roomBlobs) {
  Write-Entry $bw $r.Name $running $r.Data.Length $r.W $r.H $r.W $r.H 0 0 1
  $running += $r.Data.Length
}
foreach ($f in $frames)    { if ($f.Data.Length) { $bw.Write($f.Data) } }
foreach ($r in $roomBlobs) { $bw.Write($r.Data) }
$bw.Flush(); $fs.Close()

# verify the declared offsets actually line up with the file we just wrote
$expected = $HEADER_BYTES + ($totalEntries * $ENTRY_BYTES)
foreach ($f in $frames)    { $expected += $f.Data.Length }
foreach ($r in $roomBlobs) { $expected += $r.Data.Length }
$actual = (Get-Item $pakPath).Length
if ($expected -ne $actual) {
  Write-Output "ERROR: index/data size mismatch - expected $expected bytes, file is $actual"
} else {
  Write-Output "index offsets verified against file size ($actual bytes)"
}

$pakKB = [math]::Round($actual / 1KB, 1)
Write-Output "wrote $pakPath  ($pakKB KB, $totalEntries entries)"

# ---------------- character body widths (CHARACTER-PACKS.md §4) ----------------
#
# The firmware's walkable model constrains a POINT. bounds() clamps the feet to a rectangle and
# clearOfBlocks() only ever pushes them down in y; neither knows how wide the animal is. That
# held together only because BOUNDS_*/BLOCKS_* were hand-fitted to one animal — at adult scale
# the bunny's right edge lands at 319.8 on a 320px screen, a pixel of slack.
#
# So the firmware carries the bunny's width as BASE_BODY_W and insets every other species by the
# DIFFERENCE. Two things have to stay true for that to keep working, and both are checked here
# because a rebuild is the last moment either can be caught cheaply.
$BASE_BODY_W = 27
# The three frames petFrameKey() returns — the standing silhouette at each phase, and what the
# 27 was measured from.
$BASE_REF = @('baby-idle/0', 'teen-idle/0', 'idle-anim/0')

function Get-FrameBodyW($f) {
  # spriteBlit() anchors the UNTRIMMED canvas centre on the feet, so the opaque box spans
  # [-origW/2 + offX, +w) around him. Integer division matches the firmware's exactly.
  $left  = -[int][math]::Truncate($f.OrigW / 2) + $f.OffX
  $right = $left + $f.W
  $half  = [math]::Max([math]::Abs($left), [math]::Abs($right))
  return $half * 2
}

# 1. The calibration must not drift. If a redrawn idle makes the bunny wider or narrower, every
#    hand-tuned constant silently stops meaning what it meant, and nothing else would say so.
$refW = 0; $refFrame = ''
foreach ($f in $frames) {
  if ($BASE_REF -contains $f.Name) {
    $w = Get-FrameBodyW $f
    if ($w -gt $refW) { $refW = $w; $refFrame = $f.Name }
  }
}
if ($refW -eq 0) {
  Write-Output "ERROR: none of $($BASE_REF -join ', ') are in this pack - cannot verify BASE_BODY_W"
} elseif ($refW -ne $BASE_BODY_W) {
  Write-Output "ERROR: BASE_BODY_W is $BASE_BODY_W in main.cpp but this art measures $refW ($refFrame)."
  Write-Output "       BOUNDS_*/BLOCKS_* are fitted to the old number. Re-tune them or restore the width."
} else {
  Write-Output "body width: base calibration $refW px verified ($refFrame)"
}

# 2. Each species' width, measured from its widest trimmed frame — the number the firmware
#    derives at mount and insets the room by.
#
#    This is only a BODY measurement while §2 holds and objects are separate sprites. The legacy
#    base pack proves the point: its widest frames are teen-hungry/3 at 68 and play/south at 58,
#    both inflated by a prop drawn into the frame (the speech bubble, the ball), not by the
#    animal. A species frame with a prop baked in would inset the walkable area by the width of
#    the prop and strand the character in the middle of the room. So flag any species whose
#    widest frame runs far past its own idle — that is the signature of a prop that should have
#    been an attach point.
foreach ($id in $SpeciesIds) {
  $mine = @($frames | Where-Object { $_.Name.StartsWith("$id/") })
  if ($mine.Count -eq 0) { Write-Output "WARNING: species '$id' declared but has no frames"; continue }
  $wide = 0; $wideName = ''
  foreach ($f in $mine) { $w = Get-FrameBodyW $f; if ($w -gt $wide) { $wide = $w; $wideName = $f.Name } }
  $idle = 0
  foreach ($f in $mine) {
    if ($f.Name -match "^$id/(baby-idle|teen-idle|idle-anim)/") {
      $w = Get-FrameBodyW $f; if ($w -gt $idle) { $idle = $w }
    }
  }
  Write-Output ("body width: {0,-8} body_w={1} px (idle {2}, base {3}) widest={4}" -f $id, $wide, $idle, $BASE_BODY_W, $wideName)
  if ($idle -gt 0 -and $wide -gt $idle * 1.5) {
    Write-Output ("WARNING: {0}'s widest frame is {1}px against an idle of {2}px - looks like a prop is drawn" -f $id, $wide, $idle)
    Write-Output  "         into the frame. Objects are separate sprites (§2); this inflates the walkable inset."
  }
}

# ---------------- summary ----------------
$spriteBytes = 0
$opaque = 0
foreach ($f in $frames) { $spriteBytes += $f.Data.Length; $opaque += $f.OpaquePx }
$roomTotal = 0
foreach ($r in $roomBlobs) { $roomTotal += $r.Data.Length }
$rawBytes = 0
foreach ($f in $frames) { $rawBytes += $f.OrigW * $f.OrigH * 2 }
Write-Output ""
Write-Output "=== summary ==="
Write-Output ("  sprites:  {0} KB packed   (untrimmed RGB565 would be {1} KB)" -f `
  [math]::Round($spriteBytes/1KB,1), [math]::Round($rawBytes/1KB,1))
Write-Output ("  opaque pixels: {0}  -> {1} bytes/opaque px incl. RLE overhead" -f `
  $opaque, [math]::Round($spriteBytes/[double]$opaque, 2))
Write-Output ("  rooms:    {0} KB" -f [math]::Round($roomTotal/1KB,1))
Write-Output ("  TOTAL:    {0} KB of a 3072 KB app partition" -f `
  [math]::Round(($spriteBytes + $roomTotal)/1KB,1))
