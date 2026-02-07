# shabat-relay (ESP8266) — Developer README

Firmware for an ESP8266 relay module that acts as a **שעון שבת**:

- **Relay closes** on **Shabbat + Yom‑Tov** (holidays), using **embedded** (offline) data:
  - Zmanim table (שקיעה/צאת) generated from `data/zmanim.csv`
  - Yom‑Tov date list (~100 years, Israel) generated from `data/holidays_yomtov.csv`
- Built-in **web dashboard (Hebrew, RTL)** for Wi‑Fi, time, and settings
- **Auto Wi‑Fi setup**: if it can’t connect, it starts a hotspot

End-user instructions (product manual): `USER_MANUAL.md`.

## Hardware notes

- Default relay GPIO: `GPIO5` (D1 on many ESP8266 boards)
- Default relay logic: **Active‑Low**
- You can change both in the web UI.

### USB‑TTL cable warning

Many 4‑wire USB‑TTL cables use:

- Red = **5V**
- Black = GND
- Green/White = TX/RX (varies by cable)

ESP‑12F UART pins are **3.3V**. If your cable is 5V logic, use a level shifter (or at least a divider on ESP RX),
and preferably power the board separately (share GND).

Common PL2303-TTL cable mapping (verify yours):

- Green = TXD (connect to ESP RX / GPIO3)
- White = RXD (connect to ESP TX / GPIO1)

## Build & flash (PlatformIO)

1. Install PlatformIO (VS Code extension) or CLI.
2. Build firmware:
   - `pio run`
3. Upload firmware:
   - `pio run -t upload`

Note: The dashboard and calendar/zmanim data are embedded in the firmware, so **`uploadfs` is not required** for normal use.

If you flash manually with `esptool.py`, you only need the firmware image.

`uploadfs` is meant for development and will overwrite LittleFS (saved Wi‑Fi list + history log + OTA state + config).

Optional (development): build a filesystem image from `data/`:

- `pio run -t buildfs`

If upload times out (“Failed to connect to ESP8266”), the board likely doesn’t support auto-bootloader.
Put it in **flash mode** manually:

- Hold **GPIO0 = GND** (often a “FLASH” button), then press **RESET** (or power-cycle), then run upload again.

If you suspect the USB-serial link is unstable, try slower upload environments:

- `pio run -e esp12e_57600 -t upload`

If auto-reset wiring is unreliable, use manual-flash environment (disables esptool reset toggling):

- `pio run -e esp12e_manual -t upload`

If it connects, prints chip info, then fails around “Uploading stub…”, use no-stub environments:

- `pio run -e esp12e_manual_nostub -t upload`
- `pio run -e esp12e_manual_nostub_57600 -t upload`

## User manual

For product operation & settings, see `USER_MANUAL.md`.

Quick health check (no UI): `GET /status.txt` returns one word like `OK`, `TIME_INVALID`, `AP_MODE`.

## LEDs (outside UI)

This firmware uses **two** LEDs (plus the relay LED on the module):

### Wi‑Fi LED (ESP blue LED)

Default: `GPIO2` (Active‑Low on many ESP‑12 modules)

- Fast blink: trying to connect / not connected yet
- Double blink: Hotspot (AP) mode is active
- Short blink every ~3s: connected to Wi‑Fi

### Clock/System LED (board LED)

Default: `GPIO16` (Active‑Low on many relay boards)

- Fast blink: time not set
- Two slow blinks: waiting for first NTP sync (Wi‑Fi connected)
- 1Hz slow blink: NTP looks stale vs configured resync interval
- Triple blink: missing zmanim data (should not happen in normal builds)
- Slow blink every ~3s: missing holidays data (should not happen in normal builds)

## Embedded zmanim

The firmware includes a built-in (offline) month/day zmanim table generated from `data/zmanim.csv`.
It works for any Gregorian year by matching the current month/day.

## Embedded holidays

The firmware includes a built-in (offline) Yom‑Tov date list (~100 years, Israel) generated from `data/holidays_yomtov.csv`.
No internet fetch is required at runtime.

## OTA updates (GitHub/HTTP)

The firmware supports **HTTP OTA updates** using a small JSON manifest URL.

This repo includes a GitHub Action that builds and publishes OTA assets on every push to `main`:
- `.github/workflows/ota-release.yml` uploads `firmware.bin` + `ota.json` to a GitHub Release.

### Manifest format

Host a JSON file (for example `ota.json`) with:

```json
{
  "version": "0.1.1",
  "bin": "https://github.com/<you>/<repo>/releases/latest/download/firmware.bin",
  "md5": "optional-md5-of-firmware.bin",
  "notes": "optional release notes"
}
```

Then, in the dashboard under **עדכוני תוכנה (OTA)**:

- Enable **Auto** (optional)
- Choose **Check interval (hours)**

The dashboard intentionally does not expose the manifest URL (product UX). To set it:

- Update `ota.manifestUrl` in the device config via `POST /api/config`, or
- Ship your firmware with a non-empty default in `src/AppConfig.h` (this repo defaults to `https://github.com/yk8-git/smart-shabat/releases/latest/download/ota.json`).

### Local OTA over Hotspot (dev)

When your laptop is connected to the device Hotspot (`192.168.4.1`) and the ESP8266 is not on your home Wi‑Fi yet,
you can still test OTA by serving `ota.json` + `firmware.bin` from your laptop:

- Start a local web server on your laptop (example): `python3 -m http.server 8000`
- Tell the device to use the requester's IP as its manifest host:
  - `POST /api/ota/manifest_from_client` with body `{"port":8000,"path":"/ota.json"}`

Safety rules:

- Auto-update runs only when the device is **not in שבת/חג** (requires a valid clock + embedded zmanim + embedded holidays).
- OTA updates flash **firmware only** (does **not** erase LittleFS user data like saved Wi‑Fi list, history log, OTA state, and config).

Note: The current implementation uses HTTPS with `setInsecure()` for simplicity (no certificate pinning).

## Safety

You’re switching mains voltage. Use proper enclosure, strain relief, and wiring practices.

## Note about `uploadfs`

`pio run -t uploadfs` writes a full LittleFS image and **overwrites user data** stored on LittleFS (saved Wi‑Fi list,
history log, OTA state, and config). Prefer using it only when you intentionally want to replace the filesystem.

The web dashboard is served from **embedded assets** in firmware (`src/EmbeddedUi.h`), so `uploadfs` is not required for normal use.

If you edit `data/index.html`, `data/styles.css`, or `data/app.js`, regenerate the embedded header:

`python3 tools/gen_embedded_ui.py`
