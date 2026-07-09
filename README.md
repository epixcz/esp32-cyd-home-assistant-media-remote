# CYD Home Assistant Media Remote

An ESP32 touch-screen remote for a Home Assistant `media_player` entity (Spotify, Sonos, anything with media attributes). Shows the current track with cover art and lets you control playback right from the display.

| ESP32-2432S028R (2.8") | ESP32-3248S035C (3.5") |
|---|---|
| ![2.8" CYD showing the now-playing screen](docs/esp32-2432s028r.jpg) | ![3.5" board showing the now-playing screen](docs/esp32-3248s035c.jpg) |

## Features

- **Live updates over WebSocket** — subscribes to the Home Assistant WebSocket API (server-side filtered triggers, including attribute changes), so track changes appear instantly; REST polling remains as a fallback safety net
- **Cover art** of any size — JPEG is decoded straight from the HTTP stream (~4 kB workspace, no filesystem, no size limit)
- **Touch controls**: previous / play-pause / next / volume modal (tap-and-hold repeats), tap the progress bar to seek
- **Progress bar** extrapolated from `media_position` + `media_position_updated_at` (NTP-synced), so it stays accurate between updates
- **Diacritics-safe text** — UTF-8 titles are transliterated to ASCII (č→c, ř→r, …) for the bitmap fonts
- **Auto-dimming backlight** — full brightness while playing or after a touch, dimmed when idle
- **WiFiManager captive portal** for all configuration (no secrets in the source)
- **Opt-in OTA updates** with a separate per-device password
- Robust reconnect handling for both Wi-Fi and the WebSocket

## Supported boards

| Board | Display | Touch | PlatformIO env | Where to buy |
|---|---|---|---|---|
| **ESP32-2432S028R** — "Cheap Yellow Display" (2.8", 320×240) | ILI9341 | XPT2046 (resistive) | `cyd` (or `cyd2usb` for the USB-C variant with inverted colors) | [AliExpress](https://www.aliexpress.com/item/1005007774435209.html) |
| **ESP32-3248S035C** (3.5", 480×320) | ST7796 | GT911 (capacitive) | `esp32_3248s035c` | [AliExpress](https://www.aliexpress.com/item/1005008624700714.html) |

Not sure which 2.8" variant you have? Flash `cyd` first; if colors look inverted (white background instead of black), use `cyd2usb`.

Diagnostic firmwares are available for troubleshooting: `diag` / `diag_esp32_3248s035c` (display color cycle) and `touchdiag_esp32_3248s035c` (GT911 touch test).

## Getting started

### 1. Get a Home Assistant token

In Home Assistant: **your profile (bottom left) → Security → Long-lived access tokens → Create token**. Copy the token value — it is shown only once. Do not include the word `Bearer`, just the token itself.

### 2. Flash the firmware

Install [PlatformIO](https://platformio.org/install/cli), connect the board via USB, then:

```sh
# 2.8" CYD
pio run -e cyd -t upload

# 3.5" board
pio run -e esp32_3248s035c -t upload
```

### 3. Configure via the captive portal

On first boot the device starts a Wi-Fi access point:

- **SSID:** `CYD-HA-Media`
- **Password:** a new random value shown only on the device display

Connect to it and the portal opens automatically (or visit `http://192.168.4.1`). Fill in:

| Field | Example |
|---|---|
| Wi-Fi network + password | your home Wi-Fi |
| HA URL | `http://homeassistant.local:8123` |
| HA token | the long-lived token from step 1 |
| media_player entity | `media_player.spotify_your_name` |
| HA TLS SHA-256 fingerprint | required only for an `https://` HA URL |
| Enable OTA | off by default; enable only when OTA updates are needed |
| New OTA password | at least 12 characters; required when first enabling OTA |

The configuration is stored on the device (LittleFS). To reopen the portal
later, hold the **BOOT** button while pressing reset. Every portal session gets
a new random access-point password. When editing an existing configuration,
leave the HA token and OTA password fields blank to keep their current values;
the existing secrets are never rendered into the page.

Plain HTTP is supported for a trusted LAN, but it does not protect the HA token
from another device able to observe that network. HTTPS verifies both the HA
hostname and a provisioned SHA-256 certificate fingerprint before sending any
authenticated REST or WebSocket data.

For an HTTPS endpoint, obtain the SHA-256 fingerprint from a trusted computer
and paste either the 64 hexadecimal characters or the colon-separated form into
the portal:

```sh
openssl s_client -connect homeassistant.example:443 -servername homeassistant.example </dev/null 2>/dev/null \
  | openssl x509 -noout -fingerprint -sha256
```

Verify the value through a separate trusted channel before saving it. A renewed
or replaced HA certificate has a new fingerprint; hold **BOOT** during reset to
update it. A missing or mismatched fingerprint makes HTTPS fail closed before
the bearer token is sent.

### 4. OTA updates (optional)

OTA is disabled by default. Reopen the portal with the physical **BOOT** button,
enable OTA and set a new password of at least 12 characters. Only its MD5 hash,
which is required by ArduinoOTA authentication, is stored on the device.

Set the target and plaintext password only in your local shell before upload:

```sh
export CYD_OTA_HOST='device-ip-or-hostname'
export CYD_OTA_PASSWORD='your-new-device-specific-password'

# 2.8" board
pio run -e cyd_ota -t upload

# 3.5" board
pio run -e esp32_3248s035c_ota -t upload
```

After installing this version, the former shared OTA setup is disabled by
migration. Flash once over USB if necessary, then use the BOOT portal to opt in
with a new device-specific password. Disabling OTA in the portal removes the
stored hash and stops the OTA service. If a configuration is lost or invalid,
the BOOT portal remains the recovery path. Rotate the HA token only when the
portal was exposed in an untrusted environment or there is another concrete
reason to suspect disclosure.

## Faster updates for external changes (recommended)

Home Assistant's Spotify integration polls the Spotify Web API roughly every 30 s, so track changes made **outside** HA (on your PC or phone) reach the display with up to 30 s delay. Changes made through HA or the remote itself are instant. To shorten the delay, add this automation in HA (Settings → Automations → new, edit in YAML):

```yaml
alias: "Spotify - frequent state refresh"
description: "Speeds up the CYD display's reaction to changes made outside HA"
trigger:
  - platform: time_pattern
    seconds: "/5"
condition:
  - condition: state
    entity_id: media_player.spotify_your_name
    state: playing
action:
  - service: homeassistant.update_entity
    target:
      entity_id: media_player.spotify_your_name
mode: single
```

## How it works

- On boot the device connects to Wi-Fi, syncs NTP (UTC, needed for progress extrapolation) and fetches the initial state over REST (`/api/states/<entity>`).
- It then opens a WebSocket to `/api/websocket`, authenticates with the token and sends `subscribe_trigger` with state triggers for the entity **and** its `media_title`, `media_position` and `volume_level` attributes (a plain state trigger ignores attribute-only changes — that's why the extra triggers are needed).
- While the WebSocket is healthy, REST polling drops to a 60 s safety interval; on disconnect it falls back to 3 s until the socket reconnects.
- State JSON is parsed with an ArduinoJson filter (Spotify entities carry a huge `source_list` that would otherwise blow the buffer).
- Cover art (`entity_picture`) is fetched through an explicit redirect loop and decoded with the low-level tjpgd API directly from the HTTP stream. Every redirect hop gets a fresh HTTP client; the auth token is attached only when that hop has the exact configured HA scheme, host and port. HTTPS-to-HTTP downgrades and redirect loops are rejected.

## Development

```sh
python3 -m pip install -r requirements-dev.txt

pio run                        # build the default env (cyd)
pio run -e esp32_3248s035c     # build for the 3.5" board
pio device monitor             # serial console, 115200 baud
```

Before committing, run the same complete build matrix as CI:

```sh
pio test -c platformio.test.ini -e native
pio run -e cyd -e cyd2usb -e diag -e esp32_3248s035c -e diag_esp32_3248s035c -e touchdiag_esp32_3248s035c
```

The native suite covers overflow-safe progress, timer rollover, URL-origin and
credential-update policies without hardware. The six firmware environments
cover both production board variants and all diagnostic firmwares. Dependency
updates should be made separately from feature changes, then verified with the
native suite, full build matrix and a smoke test on both physical boards.

Everything lives in `src/main.cpp`; the board-specific layout constants are at the top of the main firmware section, guarded by `PANEL_*` defines. Diagnostic firmwares are selected with the `DIAGNOSTIC_TFT` / `DIAGNOSTIC_GT911` build flags (see `platformio.ini`).

Notes for contributors:

- `lib_ldf_mode = deep` is required — the WebSockets library isn't found by `deep+`.
- `board_build.partitions = min_spiffs.csv` gives the app 1.9 MB (needed) and keeps a small LittleFS for the config file. Changing the partition table wipes the stored configuration.
- TFT_eSPI is configured entirely through build flags (`-DUSER_SETUP_LOADED`), no `User_Setup.h` editing needed.

## Implementation log

- **Plan 001 — reproducible build gate:** pinned PlatformIO Core, the ESP32
  platform and all external libraries to the versions verified by the audit;
  added a six-environment GitHub Actions build matrix and documented its exact
  local equivalent. Hardware behavior was not changed by this plan.
- **Plan 002 — tested core policies:** introduced an Arduino-independent core
  library and native Unity tests; progress width now uses 64-bit arithmetic,
  timer deadlines survive the `millis()` rollover, and HA authorization checks
  compare parsed URL origins instead of string prefixes. Hardware smoke testing
  is still required for display timing and long-running operation.
- **Plan 003 — verified HA transport:** HTTPS REST and WebSocket connections now
  require the provisioned SHA-256 certificate fingerprint, validated before any
  HA token is sent. Cover redirects are resolved explicitly with a fresh client
  per hop, cross-origin hops drop authorization, downgrade/loop/limit cases are
  rejected, and native policy tests cover fingerprint and redirect handling.
  The audit plan mentioned SHA-1, but the pinned ESP32 framework actually
  verifies SHA-256; the implementation follows the framework's stronger API.
- **Plan 004 — isolated provisioning and OTA credentials:** every captive-portal
  session now uses a random RAM-only password displayed on the TFT, secret form
  fields are never prefilled, and blank edits safely retain existing values.
  OTA is disabled for old and new configurations until explicitly enabled with
  a separate password; only its ArduinoOTA-compatible hash is persisted. OTA
  upload host and plaintext authentication now come from local environment
  variables instead of tracked project values.
