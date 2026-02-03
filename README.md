# Chess Puzzles (CrossPoint App)

This is the standalone Chess Puzzles app for CrossPoint (Xteink X4 / ESP32-C3).

This repo is meant to be developed and released independently of the main CrossPoint firmware.

## Build

Recommended SDK: `https://github.com/open-x4-epaper/community-sdk`

Typical setup:

1. Add the SDK as a submodule:
   ```bash
   git submodule add https://github.com/open-x4-epaper/community-sdk.git open-x4-sdk
   ```
2. Build with PlatformIO:
   ```bash
   pio run
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

For end-user distribution, the recommended approach is to publish an `assets.zip` that unpacks to `/.crosspoint/chess/`.
