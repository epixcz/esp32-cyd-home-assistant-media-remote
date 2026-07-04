# CYD Home Assistant Media Remote

ESP32 Cheap Yellow Display remote for a Home Assistant `media_player` entity.

This first version is intentionally simple:

- reads currently playing media from Home Assistant REST API
- shows title, artist, album, progress and cover art
- controls previous, play/pause, next and volume
- stores Home Assistant settings through WiFiManager captive portal

## Required Home Assistant data

Use a media player entity that already exposes Spotify data in Home Assistant, for example:

```text
media_player.spotify_your_name
```

The sketch reads these attributes when available:

- `media_title`
- `media_artist`
- `media_album_name`
- `media_duration`
- `media_position`
- `entity_picture`
- `volume_level`

## First boot setup

1. Flash the `cyd` PlatformIO environment.
2. Connect to the Wi-Fi AP named `CYD-HA-Media`.
3. Password: `thing123`
4. Configure Wi-Fi plus:
   - Home Assistant URL, for example `http://homeassistant.local:8123`
   - Long-lived access token
   - media player entity id, for example `media_player.spotify`

The config is saved to SPIFFS as `/ha_media_config.json`.

To force the configuration portal later, hold the BOOT button while resetting the ESP32.

## Home Assistant token

Create a long-lived access token in Home Assistant:

`Profile -> Security -> Long-lived access tokens`

Paste only the token value. Do not include `Bearer`.

## Notes

HTTPS Home Assistant URLs are accepted with certificate verification disabled. A local HTTP URL is simpler and usually enough on a trusted LAN.
