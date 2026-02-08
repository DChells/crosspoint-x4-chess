# Chess Puzzles (CrossPoint App)

This is the standalone Chess Puzzles app for CrossPoint (Xteink X4 / ESP32-C3).

This repo is meant to be developed and released independently of the main CrossPoint firmware.

## Build

This repo uses PlatformIO.

Typical setup:

1. Init submodules:
   ```bash
   git submodule update --init --recursive
   ```
2. Build with PlatformIO:
   ```bash
   pio run -e chess-puzzles
   ```

The output binary is typically:

`./.pio/build/<env>/firmware.bin`

For CrossPoint app installs, publish/upload it as `app.bin`.

## Install on device (developer workflow)

1. Build `firmware.bin`.
2. Use CrossPoint File Transfer → **Apps (Developer)** to upload the binary.
   - App ID: `chess` (or `chess-puzzles`)
   - Upload file: `firmware.bin`
3. On device: Home → Apps → select Chess → Install.

## Assets

Chess Puzzles expects these paths on the SD card:

Sprites:

`/.crosspoint/chess/sprites/*.bin` (12 sprite files)

Puzzle packs:

`/.crosspoint/chess/packs/*.cpz`

Theme index (optional, enables theme selection UI):

`/.crosspoint/chess/index/<packName>/theme_<theme>.bit`

For end-user distribution, the recommended approach is to publish an `assets.zip` that unpacks to `/.crosspoint/chess/`.

## Install on device (PR #679 Apps workflow)

CrossPoint PR #679 adds an Apps page that supports uploading a ZIP containing `app.bin` + `app.json`.

Releases publish:
- `app.zip` (contains `app.bin` + `app.json`) for **WiFi upload**
- `sdcard.zip` (full SD layout) for **unzip-to-SD** installs
- `assets.zip` (legacy) that unpacks to `/.crosspoint/chess/`

WiFi upload:
1. Device: Home -> File Transfer -> Apps
2. Upload `app.zip`
3. On device: Home -> Apps -> Chess Puzzles -> Install

SD card:
1. Unzip `sdcard.zip` to the root of your SD card (creates `/.crosspoint/apps/chess-puzzles/` and `/.crosspoint/chess/`)
2. On device: Home -> Apps -> Chess Puzzles -> Install

This repo includes a small starter pack:

- `assets/packs/starter.cpz`
- `assets/index/starter/*.bit`

## Generate Puzzle Packs (Lichess)

This app can consume packs generated from the public Lichess puzzle database CSV (`lichess_db_puzzle.csv`).

Tooling:

- `tools/pack_lichess_cpz.py` - builds a `.cpz` pack (CPZ1) and optional theme bitsets
- `tools/inspect_cpz.py` - quick validation/debug output

Examples:

Generate a pack from Lichess CSV:
```bash
python3 tools/pack_lichess_cpz.py \
  --input lichess_db_puzzle.csv \
  --output assets/packs/lichess_1400_1600.cpz \
  --out-dir assets \
  --min-rating 1400 \
  --max-rating 1600 \
  --limit 2000
```

Generate the built-in starter pack:
```bash
python3 tools/pack_lichess_cpz.py --starter --output assets/packs/starter.cpz --out-dir assets
```
