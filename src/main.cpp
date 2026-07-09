#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <MD5Builder.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <esp_system.h>

#include <MediaRemoteCore.h>

#if defined(PANEL_CYD_2432S028R)
#include "CYD28_TouchscreenR.h"
#endif

#if defined(PANEL_ESP32_3248S035C)
#include <TAMC_GT911.h>
#endif

#if defined(DIAGNOSTIC_GT911)

TFT_eSPI tft = TFT_eSPI();

constexpr uint8_t GTD_ADDRS[] = {0x5D, 0x14};
constexpr int GTD_SDA = 33;
constexpr int GTD_SCL = 32;
constexpr int GTD_INT = 21;
constexpr int GTD_RST = 25;
constexpr uint16_t GTD_REG_STATUS = 0x814E;
constexpr uint16_t GTD_REG_POINT1 = 0x814F;
uint8_t gtdAddress = 0;
uint32_t gtdFrames = 0;
TAMC_GT911 gtdTouch(GTD_SDA, GTD_SCL, GTD_INT, GTD_RST, 480, 320);

bool gtdRead(uint8_t addr, uint16_t reg, uint8_t *data, size_t len)
{
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, (uint8_t)len) != len) {
    return false;
  }
  for (size_t i = 0; i < len && Wire.available(); i++) {
    data[i] = Wire.read();
  }
  return true;
}

bool gtdWrite(uint8_t addr, uint16_t reg, uint8_t value)
{
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

void gtdResetHigh()
{
  pinMode(GTD_INT, OUTPUT);
  digitalWrite(GTD_INT, HIGH);
  pinMode(GTD_RST, OUTPUT);
  digitalWrite(GTD_RST, LOW);
  delay(20);
  digitalWrite(GTD_RST, HIGH);
  delay(120);
  pinMode(GTD_INT, INPUT);
}

void gtdScan()
{
  gtdAddress = 0;
  Serial.println("I2C scan start");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device 0x%02X\n", addr);
      for (uint8_t i = 0; i < sizeof(GTD_ADDRS); i++) {
        if (addr == GTD_ADDRS[i]) {
          gtdAddress = addr;
        }
      }
    }
  }
  Serial.printf("GT911 selected addr=0x%02X\n", gtdAddress);
}

void gtdDrawHeader(const String &extra = "")
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(16, 16);
  tft.print("GT911 TOUCH DIAG");
  tft.setTextSize(1);
  tft.setCursor(16, 54);
  tft.print("SDA=33 SCL=32 INT=21 RST=25");
  tft.setCursor(16, 76);
  tft.print("addr=");
  if (gtdAddress) {
    tft.printf("0x%02X", gtdAddress);
  } else {
    tft.print("not found");
  }
  if (extra.length()) {
    tft.setCursor(16, 98);
    tft.print(extra);
  }
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_DARKGREY);
  tft.drawFastHLine(0, 125, tft.width(), TFT_DARKGREY);
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  Wire.begin(GTD_SDA, GTD_SCL);
  Wire.setClock(400000);
  gtdTouch.begin(GT911_ADDR1);
  gtdTouch.setRotation(ROTATION_RIGHT);
  gtdScan();

  String product = "product read failed";
  if (gtdAddress) {
    uint8_t pid[4] = {0};
    if (gtdRead(gtdAddress, 0x8140, pid, sizeof(pid))) {
      product = "product ";
      product += (char)pid[0];
      product += (char)pid[1];
      product += (char)pid[2];
      product += (char)pid[3];
      Serial.printf("GT product %c%c%c%c\n", pid[0], pid[1], pid[2], pid[3]);
    }
  }
  gtdDrawHeader(product);
}

void loop()
{
  gtdFrames++;
  if (!gtdAddress) {
    delay(1000);
    gtdTouch.begin(GT911_ADDR1);
    gtdTouch.setRotation(ROTATION_RIGHT);
    gtdScan();
    gtdDrawHeader("rescanning...");
    return;
  }

  gtdTouch.read();
  if (!gtdTouch.isTouched || gtdTouch.touches == 0) {
    if (gtdFrames % 20 == 0) {
      tft.fillRect(16, 140, 440, 28, TFT_BLACK);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setTextSize(1);
      tft.setCursor(16, 140);
      tft.printf("waiting... isTouched=%d touches=%u", gtdTouch.isTouched, gtdTouch.touches);
      Serial.printf("GT wait isTouched=%d touches=%u\n", gtdTouch.isTouched, gtdTouch.touches);
    }
    delay(50);
    return;
  }

  TP_Point p = gtdTouch.points[0];
  Serial.printf("GT touch touches=%u x=%u y=%u size=%u\n", gtdTouch.touches, p.x, p.y, p.size);

  tft.fillRect(0, 128, 480, 192, TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(16, 142);
  tft.printf("touches %u", gtdTouch.touches);
  tft.setCursor(16, 178);
  tft.printf("x %u", p.x);
  tft.setCursor(16, 214);
  tft.printf("y %u", p.y);
  tft.setCursor(16, 250);
  tft.printf("size %u", p.size);
  if (p.x < 480 && p.y < 320) {
    tft.fillCircle(p.x, p.y, 9, TFT_RED);
  }
  delay(80);
}

#elif defined(DIAGNOSTIC_TFT)

TFT_eSPI tft = TFT_eSPI();

const uint16_t diagColors[] = {
  TFT_RED,
  TFT_GREEN,
  TFT_BLUE,
  TFT_WHITE,
  TFT_YELLOW,
  TFT_CYAN,
  TFT_MAGENTA,
  TFT_BLACK,
};

const char *diagColorNames[] = {
  "RED",
  "GREEN",
  "BLUE",
  "WHITE",
  "YELLOW",
  "CYAN",
  "MAGENTA",
  "BLACK",
};

void drawDiagFrame(const char *backlightState, int rotation, int colorIndex)
{
  tft.setRotation(rotation);
  tft.fillScreen(diagColors[colorIndex]);
  tft.setTextColor(diagColors[colorIndex] == TFT_BLACK ? TFT_WHITE : TFT_BLACK, diagColors[colorIndex]);
  tft.setTextSize(2);
  tft.setCursor(12, 18);
  tft.print("CYD TFT DIAG");
  tft.setTextSize(1);
  tft.setCursor(12, 58);
  tft.print("Backlight: ");
  tft.print(backlightState);
  tft.setCursor(12, 78);
  tft.print("Rotation: ");
  tft.print(rotation);
  tft.setCursor(12, 98);
  tft.print("Color: ");
  tft.print(diagColorNames[colorIndex]);
  tft.drawRect(4, 4, tft.width() - 8, tft.height() - 8, diagColors[colorIndex] == TFT_BLACK ? TFT_WHITE : TFT_BLACK);
  tft.drawLine(0, 0, tft.width() - 1, tft.height() - 1, diagColors[colorIndex] == TFT_BLACK ? TFT_WHITE : TFT_BLACK);
  Serial.printf("DIAG backlight=%s rotation=%d color=%s\n", backlightState, rotation, diagColorNames[colorIndex]);
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("CYD TFT diagnostic firmware");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_RED);
  Serial.printf("TFT width=%d height=%d\n", tft.width(), tft.height());
}

void loop()
{
  for (int backlight = 0; backlight < 2; backlight++) {
    bool high = backlight == 0;
    digitalWrite(TFT_BL, high ? HIGH : LOW);
    const char *backlightState = high ? "HIGH" : "LOW";

    for (int rotation = 0; rotation < 4; rotation++) {
      for (int colorIndex = 0; colorIndex < 8; colorIndex++) {
        drawDiagFrame(backlightState, rotation, colorIndex);
        delay(900);
      }
    }
  }
}

#else

#define CONFIG_FILE "/ha_media_config.json"
#define AP_NAME "CYD-HA-Media"

#if defined(PANEL_ESP32_3248S035C)
#define SCREEN_W 480
#define SCREEN_H 320
#define COVER_SIZE 164
#define COVER_X ((SCREEN_W - COVER_SIZE) / 2)
#define COVER_Y 26
#define TEXT_X 14
#define TEXT_Y 196
#define TEXT_BLOCK_H 66
#define TITLE_MAX_LEN 38
#define ARTIST_MAX_LEN 40
#define ALBUM_MAX_LEN 44
#define TEXT_SIZE_TITLE 2
#define TEXT_SIZE_ARTIST 2
#define TEXT_SIZE_ALBUM 1
#define BAR_X 18
#define BAR_Y 269
#define CTRL_Y 288
#define CONTROL_TOP_Y 278
#define CONTROL_H 42
#define VOLUME_TOP_Y 246
#define VOLUME_H 74
#else
#define SCREEN_W 320
#define SCREEN_H 240
#define COVER_X 10
#define COVER_Y 42
#define COVER_SIZE 88
#define TEXT_X 108
#define TEXT_Y 42
#define TEXT_BLOCK_H 112
#define TITLE_MAX_LEN 18
#define ARTIST_MAX_LEN 20
#define ALBUM_MAX_LEN 30
#define TEXT_SIZE_TITLE 2
#define TEXT_SIZE_ARTIST 2
#define TEXT_SIZE_ALBUM 1
#define BAR_X 10
#define BAR_Y 168
#define CTRL_Y 194
#define CONTROL_TOP_Y 184
#define CONTROL_H 56
#define VOLUME_TOP_Y 180
#define VOLUME_H 60
#endif

#define BAR_W (SCREEN_W - (BAR_X * 2))
#define BTN_PREV_X (SCREEN_W / 8)
#define BTN_PLAY_X ((SCREEN_W * 3) / 8)
#define BTN_NEXT_X ((SCREEN_W * 5) / 8)
#define BTN_VOL_X ((SCREEN_W * 7) / 8)

#define COVER_FILE "/cover.jpg"

struct AppConfig {
  char haUrl[96];
  char token[384];
  char entityId[80];
  char tlsFingerprint[65];
  bool otaEnabled;
  char otaPasswordHash[33];
};

struct MediaState {
  bool available = false;
  bool playing = false;
  String state;
  String title;
  String artist;
  String album;
  String picture;
  long progressMs = 0;
  long durationMs = 0;
  int volume = 50;
  bool hasVolume = false;
};

TFT_eSPI tft = TFT_eSPI();
#if defined(PANEL_CYD_2432S028R)
CYD28_TouchR touch(SCREEN_W, SCREEN_H);
#endif
WiFiClient plainClient;
WiFiClientSecure secureClient;

AppConfig config;
MediaState currentMedia;

bool shouldSaveConfig = false;
bool showVolumeModal = false;
bool imageDisplayed = false;
bool baseUiDrawn = false;
bool touchWasDown = false;
String lastTrackKey;
String lastPicture;
unsigned long nextPollAt = 0;
unsigned long nextProgressAt = 0;
unsigned long playbackStartedAt = 0;
unsigned long volumeModalOpenedAt = 0;
unsigned long lastTouchAt = 0;
unsigned long lastWifiAttemptAt = 0;
bool wifiWasDown = false;
bool otaReady = false;
char provisioningPassword[17] = {0};
const unsigned long pollIntervalMs = 3000;
// With a live WebSocket subscription the poll is only a safety net.
const unsigned long wsPollIntervalMs = 60000;
const unsigned long progressIntervalMs = 500;
const unsigned long touchDebounceMs = 180;

WebSocketsClient ws;
bool wsConnected = false;
bool wsSubscribed = false;
unsigned long wsNextMsgId = 1;
unsigned long wsSubscriptionId = 0;

bool wsActive()
{
  return wsConnected && wsSubscribed;
}

const int backlightPwmChannel = 7;
const unsigned long backlightDimAfterMs = 30000;
const uint8_t backlightDimLevel = 40;
uint8_t backlightLevel = 0;

void setBacklight(uint8_t level)
{
  if (level != backlightLevel) {
    backlightLevel = level;
    ledcWrite(backlightPwmChannel, level);
  }
}

// Full brightness while playing or shortly after a touch, dimmed otherwise.
void updateBacklight()
{
  bool active = currentMedia.playing || millis() - lastTouchAt < backlightDimAfterMs;
  setBacklight(active ? 255 : backlightDimLevel);
}

#if defined(PANEL_ESP32_3248S035C)
struct TouchPoint {
  bool touched = false;
  int16_t x = 0;
  int16_t y = 0;
};

constexpr int GT911_SDA = 33;
constexpr int GT911_SCL = 32;
constexpr int GT911_INT = 21;
constexpr int GT911_RST = 25;
TAMC_GT911 gt911Touch(GT911_SDA, GT911_SCL, GT911_INT, GT911_RST, SCREEN_W, SCREEN_H);

void setupGt911()
{
  gt911Touch.begin(GT911_ADDR1);
  gt911Touch.setRotation(ROTATION_RIGHT);
  Wire.setClock(400000);
  Serial.println("GT911 initialized through TAMC_GT911 addr=0x5D rotation=RIGHT");
}

TouchPoint readGt911Touch()
{
  TouchPoint point;
  gt911Touch.read();
  if (!gt911Touch.isTouched || gt911Touch.touches == 0) {
    return point;
  }

  TP_Point p = gt911Touch.points[0];
  point.x = p.x;
  point.y = p.y;
  point.touched = point.x >= 0 && point.x < SCREEN_W && point.y >= 0 && point.y < SCREEN_H;
  Serial.printf("GT911 touch x=%d y=%d touches=%u valid=%d\n", point.x, point.y, gt911Touch.touches, point.touched);
  return point;
}
#endif

bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
  if (y >= tft.height() || x >= tft.width()) {
    return false;
  }
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

String trimTrailingSlash(const char *value)
{
  String out = String(value);
  out.trim();
  while (out.endsWith("/")) {
    out.remove(out.length() - 1);
  }
  return out;
}

String haBaseUrl()
{
  return trimTrailingSlash(config.haUrl);
}

String makeHaUrl(const String &path)
{
  if (path.startsWith("http://") || path.startsWith("https://")) {
    return path;
  }
  if (path.startsWith("/")) {
    return haBaseUrl() + path;
  }
  return haBaseUrl() + "/" + path;
}

void drawStatusLine(const String &message, uint16_t color);

// The HA bearer token must never be sent to third-party hosts.
bool isHaHostUrl(const String &url)
{
  String absoluteUrl = makeHaUrl(url);
  return media_remote::sameOrigin(haBaseUrl().c_str(), absoluteUrl.c_str());
}

void secureClear(char *buffer, size_t size)
{
  volatile char *cursor = buffer;
  while (cursor && size-- > 0) {
    *cursor++ = '\0';
  }
}

void generateProvisioningPassword()
{
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  constexpr size_t alphabetSize = sizeof(alphabet) - 1;
  constexpr uint8_t unbiasedLimit = 256 - (256 % alphabetSize);

  size_t written = 0;
  while (written < sizeof(provisioningPassword) - 1) {
    uint8_t randomByte;
    esp_fill_random(&randomByte, sizeof(randomByte));
    if (randomByte >= unbiasedLimit) {
      continue;
    }
    provisioningPassword[written++] = alphabet[randomByte % alphabetSize];
  }
  provisioningPassword[written] = '\0';
}

void hashOtaPassword(const char *password, char output[33])
{
  MD5Builder md5;
  md5.begin();
  md5.add(password);
  md5.calculate();
  String hash = md5.toString();
  strlcpy(output, hash.c_str(), 33);
}

bool normalizeAndValidateConfig()
{
  media_remote::Origin origin = media_remote::parseHttpOrigin(config.haUrl);
  if (!origin.valid || !config.token[0] || !config.entityId[0]) {
    return false;
  }

  if (origin.scheme == media_remote::UrlScheme::Https) {
    char normalized[65];
    if (!media_remote::normalizeSha256Fingerprint(config.tlsFingerprint, normalized, sizeof(normalized))) {
      return false;
    }
    strlcpy(config.tlsFingerprint, normalized, sizeof(config.tlsFingerprint));
  } else if (config.tlsFingerprint[0]) {
    char normalized[65];
    if (!media_remote::normalizeSha256Fingerprint(config.tlsFingerprint, normalized, sizeof(normalized))) {
      return false;
    }
    strlcpy(config.tlsFingerprint, normalized, sizeof(config.tlsFingerprint));
  }

  if (!config.otaEnabled || !media_remote::isValidMd5Hash(config.otaPasswordHash)) {
    config.otaEnabled = false;
    secureClear(config.otaPasswordHash, sizeof(config.otaPasswordHash));
  }
  return true;
}

bool beginVerifiedHaHttps(HTTPClient &http, const String &url, const media_remote::Origin &origin)
{
  if (!config.tlsFingerprint[0]) {
    Serial.println("HA TLS fingerprint missing");
    drawStatusLine("TLS fingerprint missing", TFT_RED);
    return false;
  }

  secureClient.stop();
  // The TLS handshake itself is permissive, but no HTTP bytes are sent until
  // the configured SHA-256 pin and hostname have both been verified below.
  secureClient.setInsecure();
  if (!http.begin(secureClient, url)) {
    return false;
  }
  if (!secureClient.connect(origin.host, origin.port, 2000)
      || !secureClient.verify(config.tlsFingerprint, origin.host)) {
    Serial.println("HA TLS verification failed");
    drawStatusLine("HA TLS verify failed", TFT_RED);
    secureClient.stop();
    http.end();
    return false;
  }
  return true;
}

bool beginHttp(HTTPClient &http, const String &url)
{
  // Keep timeouts short: these requests run in loop() and block the UI.
  http.setConnectTimeout(2000);
  http.setTimeout(3000);

  media_remote::Origin origin = media_remote::parseHttpOrigin(url.c_str());
  if (!origin.valid) {
    Serial.println("Invalid HTTP URL");
    return false;
  }
  if (origin.scheme == media_remote::UrlScheme::Https && isHaHostUrl(url)) {
    return beginVerifiedHaHttps(http, url, origin);
  }
  if (origin.scheme == media_remote::UrlScheme::Https) {
    // External cover hosts never receive the HA token. Their general-purpose
    // PKI trust remains outside the HA credential boundary in this firmware.
    secureClient.stop();
    secureClient.setInsecure();
    return http.begin(secureClient, url);
  }
  return http.begin(plainClient, url);
}

void addHaHeaders(HTTPClient &http)
{
  http.addHeader("Authorization", "Bearer " + String(config.token));
  http.addHeader("Content-Type", "application/json");
}

// Parses HA ISO 8601 timestamps like "2026-07-04T12:34:56.789012+00:00"
// into epoch milliseconds. Returns 0 on parse failure.
long long parseIso8601Ms(const char *value)
{
  int year, month, day, hour, minute, second;
  if (sscanf(value, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
    return 0;
  }

  struct tm tmUtc = {};
  tmUtc.tm_year = year - 1900;
  tmUtc.tm_mon = month - 1;
  tmUtc.tm_mday = day;
  tmUtc.tm_hour = hour;
  tmUtc.tm_min = minute;
  tmUtc.tm_sec = second;
  long long epochMs = (long long)mktime(&tmUtc) * 1000LL;

  const char *rest = strchr(value, ':');
  rest = rest ? strchr(rest, '.') : nullptr;
  if (rest) {
    int fracMs = 0;
    int digits = 0;
    for (const char *p = rest + 1; *p >= '0' && *p <= '9' && digits < 3; p++, digits++) {
      fracMs = fracMs * 10 + (*p - '0');
    }
    while (digits > 0 && digits < 3) {
      fracMs *= 10;
      digits++;
    }
    epochMs += fracMs;
  }

  const char *tz = strrchr(value, '+');
  if (!tz && strlen(value) > 10) {
    tz = strrchr(value + 10, '-');
  }
  if (tz) {
    int tzHour = 0, tzMin = 0;
    if (sscanf(tz + 1, "%d:%d", &tzHour, &tzMin) >= 1) {
      long offsetSec = (long)tzHour * 3600 + (long)tzMin * 60;
      epochMs += (*tz == '+' ? -1LL : 1LL) * offsetSec * 1000LL;
    }
  }
  return epochMs;
}

bool clockSynced()
{
  return time(nullptr) > 1600000000; // sanity: after year 2020
}

long long nowEpochMs()
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

// TFT_eSPI bitmap fonts are ASCII-only, so transliterate accented latin
// letters to their base form and drop anything else. Also keeps
// truncation/wrapping safe (no multi-byte chars to cut in half).
String utf8ToAscii(const char *input)
{
  // U+00C0-U+00FF, U+0100-U+013F, U+017F-U+017F (64 chars per lead byte)
  static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  static const char latinExtA[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL";
  static const char latinExtB[] = "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

  String out;
  const uint8_t *p = (const uint8_t *)input;
  while (*p) {
    uint8_t c = *p;
    if (c < 0x80) {
      out += (char)c;
      p += 1;
    } else if ((c == 0xC3 || c == 0xC4 || c == 0xC5) && (p[1] & 0xC0) == 0x80) {
      uint8_t idx = p[1] - 0x80;
      out += (c == 0xC3 ? latin1 : c == 0xC4 ? latinExtA : latinExtB)[idx];
      p += 2;
    } else if (c == 0xE2 && p[1] == 0x80 && p[2]) {
      // common punctuation: dashes, quotes, ellipsis
      uint8_t b3 = p[2];
      if (b3 == 0x93 || b3 == 0x94) {
        out += '-';
      } else if (b3 == 0x98 || b3 == 0x99) {
        out += '\'';
      } else if (b3 == 0x9C || b3 == 0x9D) {
        out += '"';
      } else if (b3 == 0xA6) {
        out += "...";
      }
      p += 3;
    } else {
      // skip remaining bytes of an unknown multi-byte sequence
      p += 1;
      while ((*p & 0xC0) == 0x80) {
        p += 1;
      }
    }
  }
  return out;
}

String truncateText(const String &text, int maxLength)
{
  if (text.length() <= maxLength) {
    return text;
  }
  return text.substring(0, maxLength - 3) + "...";
}

void drawCenteredWrappedText(String text, int y, int textSize, int maxChars, int maxLines, uint16_t color)
{
  text.trim();
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(textSize);

  const int lineHeight = 8 * textSize + 3;
  for (int lineIndex = 0; lineIndex < maxLines; lineIndex++) {
    if (text.length() == 0) {
      return;
    }

    String line;
    if (text.length() <= maxChars) {
      line = text;
      text = "";
    } else {
      int cut = maxChars;
      for (int i = maxChars; i > maxChars / 2; i--) {
        if (text.charAt(i) == ' ') {
          cut = i;
          break;
        }
      }
      line = text.substring(0, cut);
      text = text.substring(cut);
      text.trim();
    }

    if (lineIndex == maxLines - 1 && text.length() > 0) {
      line = truncateText(line, maxChars);
    }

    tft.drawCentreString(line, SCREEN_W / 2, y + lineIndex * lineHeight, 1);
  }
}

void drawCenteredMessage(const String &line1, const String &line2 = "")
{
  baseUiDrawn = false;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(SCREEN_W >= 480 ? 3 : 2);
  tft.drawCentreString(line1, SCREEN_W / 2, SCREEN_H / 2 - 28, 2);
  if (line2.length() > 0) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(SCREEN_W >= 480 ? 2 : 2);
    tft.drawCentreString(line2, SCREEN_W / 2, SCREEN_H / 2 + 8, 2);
  }
}

void drawStatusLine(const String &message, uint16_t color = TFT_LIGHTGREY)
{
  tft.fillRect(0, 0, SCREEN_W, 24, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString(message, 8, 4, 2);
}

void drawWifiManagerMessage(WiFiManager *manager)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawCentreString("Configuration mode", SCREEN_W / 2, 12, 2);
  tft.drawString("Wi-Fi AP:", 12, 48, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(manager->getConfigPortalSSID(), 92, 48, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Password:", 12, 72, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(provisioningPassword, 92, 72, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Open the captive portal", 12, 116, 2);
  tft.drawString("or visit:", 12, 138, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(WiFi.softAPIP().toString(), 92, 138, 2);
}

void drawSpeakerIcon(int x, int y, uint16_t color)
{
  tft.fillRect(x, y + 8, 6, 14, color);
  tft.fillTriangle(x + 6, y + 8, x + 6, y + 22, x + 16, y + 30, color);
  tft.fillTriangle(x + 6, y + 8, x + 16, y, x + 16, y + 30, color);
  tft.drawFastVLine(x + 21, y + 8, 14, color);
  tft.drawFastVLine(x + 25, y + 5, 20, color);
}

void drawPlayIcon(bool playing)
{
  tft.fillRect(BTN_PLAY_X - 28, CTRL_Y - 5, 56, 40, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (playing) {
    tft.fillRect(BTN_PLAY_X - 10, CTRL_Y, 8, 28, TFT_WHITE);
    tft.fillRect(BTN_PLAY_X + 4, CTRL_Y, 8, 28, TFT_WHITE);
  } else {
    tft.fillTriangle(BTN_PLAY_X - 9, CTRL_Y - 1, BTN_PLAY_X - 9, CTRL_Y + 31, BTN_PLAY_X + 17, CTRL_Y + 15, TFT_WHITE);
  }
}

void drawPreviousIcon(int x, int y, uint16_t color)
{
  tft.fillRect(x - 22, y + 2, 5, 24, color);
  tft.fillTriangle(x - 14, y + 14, x + 4, y + 2, x + 4, y + 26, color);
  tft.fillTriangle(x + 2, y + 14, x + 20, y + 2, x + 20, y + 26, color);
}

void drawNextIcon(int x, int y, uint16_t color)
{
  tft.fillRect(x + 17, y + 2, 5, 24, color);
  tft.fillTriangle(x - 20, y + 2, x - 20, y + 26, x - 2, y + 14, color);
  tft.fillTriangle(x - 4, y + 2, x - 4, y + 26, x + 14, y + 14, color);
}

void drawControls()
{
  tft.fillRect(0, CONTROL_TOP_Y, SCREEN_W, CONTROL_H, TFT_BLACK);
  tft.drawFastHLine(0, CONTROL_TOP_Y, SCREEN_W, TFT_DARKGREY);
  drawPreviousIcon(BTN_PREV_X, CTRL_Y, TFT_WHITE);
  drawPlayIcon(currentMedia.playing);
  drawNextIcon(BTN_NEXT_X, CTRL_Y, TFT_WHITE);
  drawSpeakerIcon(BTN_VOL_X - 14, CTRL_Y - 1, TFT_WHITE);
}

void drawVolumeModal()
{
  tft.fillRect(0, VOLUME_TOP_Y, SCREEN_W, VOLUME_H, TFT_BLACK);
  tft.drawRect(0, VOLUME_TOP_Y, SCREEN_W, VOLUME_H, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(SCREEN_W >= 480 ? 4 : 3);
  tft.setCursor(SCREEN_W >= 480 ? 62 : 42, VOLUME_TOP_Y + 16);
  tft.print("-");
  tft.setCursor(SCREEN_W - (SCREEN_W >= 480 ? 78 : 60), VOLUME_TOP_Y + 16);
  tft.print("+");

  const int barX = SCREEN_W >= 480 ? 150 : 94;
  const int barY = VOLUME_TOP_Y + (SCREEN_W >= 480 ? 42 : 24);
  const int barW = SCREEN_W >= 480 ? 180 : 132;
  tft.drawRect(barX, barY, barW, SCREEN_W >= 480 ? 18 : 14, TFT_DARKGREY);
  int fillW = map(currentMedia.volume, 0, 100, 0, barW - 2);
  tft.fillRect(barX + 1, barY + 1, fillW, SCREEN_W >= 480 ? 16 : 12, TFT_GREEN);
  tft.setTextSize(SCREEN_W >= 480 ? 2 : 1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawCentreString(String(currentMedia.volume) + "%", SCREEN_W / 2, VOLUME_TOP_Y + 4, 2);
}

void drawBaseInterface()
{
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(COVER_X - 1, COVER_Y - 1, COVER_SIZE + 2, COVER_SIZE + 2, TFT_DARKGREY);
  drawStatusLine(config.entityId);
  drawControls();
  lastTrackKey = "";
  lastPicture = "";
  imageDisplayed = false;
  baseUiDrawn = true;
}

void drawProgress(long progressMs, long durationMs)
{
  if (durationMs <= 0) {
    progressMs = 0;
    durationMs = 1;
  }
  if (progressMs < 0) {
    progressMs = 0;
  }
  if (progressMs > durationMs) {
    progressMs = durationMs;
  }
  int fillW = media_remote::progressBarWidth(progressMs, durationMs, BAR_W);
  tft.drawFastHLine(BAR_X, BAR_Y, BAR_W, TFT_DARKGREY);
  tft.drawFastHLine(BAR_X, BAR_Y + 1, BAR_W, TFT_DARKGREY);
  tft.drawFastHLine(BAR_X, BAR_Y, fillW, TFT_WHITE);
  tft.drawFastHLine(BAR_X, BAR_Y + 1, fillW, TFT_WHITE);
}

void drawText()
{
  String trackKey = currentMedia.title + "|" + currentMedia.artist + "|" + currentMedia.album;
  if (trackKey == lastTrackKey) {
    return;
  }
  lastTrackKey = trackKey;
  tft.fillRect(TEXT_X, TEXT_Y, SCREEN_W - TEXT_X - 8, TEXT_BLOCK_H, TFT_BLACK);
  tft.setTextWrap(false);

#if defined(PANEL_ESP32_3248S035C)
  drawCenteredWrappedText(currentMedia.title.length() ? currentMedia.title : "Nothing playing", TEXT_Y, TEXT_SIZE_TITLE, TITLE_MAX_LEN, 2, TFT_WHITE);
  drawCenteredWrappedText(currentMedia.artist, TEXT_Y + 37, TEXT_SIZE_ARTIST, ARTIST_MAX_LEN, 1, TFT_LIGHTGREY);
  drawCenteredWrappedText(currentMedia.album, TEXT_Y + 56, TEXT_SIZE_ALBUM, ALBUM_MAX_LEN, 1, TFT_DARKGREY);
#else
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(TEXT_SIZE_TITLE);
  tft.setCursor(TEXT_X, TEXT_Y);
  tft.println(truncateText(currentMedia.title.length() ? currentMedia.title : "Nothing playing", TITLE_MAX_LEN));
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(TEXT_SIZE_ARTIST);
  tft.setCursor(TEXT_X, TEXT_Y + (SCREEN_W >= 480 ? 56 : 34));
  tft.println(truncateText(currentMedia.artist, ARTIST_MAX_LEN));
  tft.setTextSize(TEXT_SIZE_ALBUM);
  tft.setCursor(TEXT_X, TEXT_Y + (SCREEN_W >= 480 ? 98 : 66));
  tft.println(truncateText(currentMedia.album, ALBUM_MAX_LEN));
#endif
}

bool loadConfig()
{
  memset(&config, 0, sizeof(config));
  if (!LittleFS.exists(CONFIG_FILE)) {
    return false;
  }

  File file = LittleFS.open(CONFIG_FILE, "r");
  if (!file) {
    return false;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.println("Failed to parse config");
    return false;
  }

  strlcpy(config.haUrl, doc["haUrl"] | "", sizeof(config.haUrl));
  strlcpy(config.token, doc["token"] | "", sizeof(config.token));
  strlcpy(config.entityId, doc["entityId"] | "", sizeof(config.entityId));
  strlcpy(config.tlsFingerprint, doc["tlsFingerprint"] | "", sizeof(config.tlsFingerprint));
  config.otaEnabled = doc["otaEnabled"] | false;
  strlcpy(config.otaPasswordHash, doc["otaPasswordHash"] | "", sizeof(config.otaPasswordHash));

  return normalizeAndValidateConfig();
}

void saveConfig()
{
  StaticJsonDocument<1024> doc;
  doc["haUrl"] = config.haUrl;
  doc["token"] = config.token;
  doc["entityId"] = config.entityId;
  doc["tlsFingerprint"] = config.tlsFingerprint;
  doc["otaEnabled"] = config.otaEnabled;
  doc["otaPasswordHash"] = config.otaEnabled ? config.otaPasswordHash : "";

  File file = LittleFS.open(CONFIG_FILE, "w");
  if (!file) {
    Serial.println("Failed to open config for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void saveConfigCallback()
{
  shouldSaveConfig = true;
}

void setupWiFiAndConfig(bool forceConfig)
{
  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(drawWifiManagerMessage);

  char tlsFingerprintInput[96];
  strlcpy(tlsFingerprintInput, config.tlsFingerprint, sizeof(tlsFingerprintInput));

  WiFiManagerParameter haUrlParam("ha_url", "HA URL", config.haUrl, sizeof(config.haUrl));
  WiFiManagerParameter tokenParam(
    "ha_token", "New HA token (blank keeps current)", "", sizeof(config.token) - 1,
    "type=\"password\" autocomplete=\"new-password\"");
  WiFiManagerParameter entityParam("entity_id", "media_player entity", config.entityId, sizeof(config.entityId));
  WiFiManagerParameter tlsFingerprintParam(
    "ha_tls_fp", "HA TLS SHA-256 fingerprint", tlsFingerprintInput, sizeof(tlsFingerprintInput));
  const char *otaCheckboxAttributes = config.otaEnabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"";
  WiFiManagerParameter otaEnabledParam(
    "ota_enabled", "Enable OTA", "1", 1, otaCheckboxAttributes, WFM_LABEL_AFTER);
  WiFiManagerParameter otaPasswordParam(
    "ota_password", "New OTA password (min. 12; blank keeps current)", "", 64,
    "type=\"password\" autocomplete=\"new-password\"");

  wm.addParameter(&haUrlParam);
  wm.addParameter(&tokenParam);
  wm.addParameter(&entityParam);
  wm.addParameter(&tlsFingerprintParam);
  wm.addParameter(&otaEnabledParam);
  wm.addParameter(&otaPasswordParam);

  generateProvisioningPassword();
  bool requirePortal = forceConfig;
  while (true) {
    shouldSaveConfig = false;
    bool connected = requirePortal
      ? wm.startConfigPortal(AP_NAME, provisioningPassword)
      : wm.autoConnect(AP_NAME, provisioningPassword);

    if (!connected) {
      secureClear(provisioningPassword, sizeof(provisioningPassword));
      ESP.restart();
    }
    if (!shouldSaveConfig) {
      secureClear(provisioningPassword, sizeof(provisioningPassword));
      return;
    }

    AppConfig previous = config;
    AppConfig proposed = config;
    strlcpy(proposed.haUrl, haUrlParam.getValue(), sizeof(proposed.haUrl));
    strlcpy(proposed.entityId, entityParam.getValue(), sizeof(proposed.entityId));

    char normalizedFingerprint[65];
    if (media_remote::normalizeSha256Fingerprint(
          tlsFingerprintParam.getValue(), normalizedFingerprint, sizeof(normalizedFingerprint))) {
      strlcpy(proposed.tlsFingerprint, normalizedFingerprint, sizeof(proposed.tlsFingerprint));
    } else {
      proposed.tlsFingerprint[0] = '\0';
    }

    media_remote::CredentialAction tokenAction = media_remote::resolveHaTokenInput(
      previous.token[0] != '\0', tokenParam.getValue());
    if (tokenAction == media_remote::CredentialAction::Replace) {
      strlcpy(proposed.token, tokenParam.getValue(), sizeof(proposed.token));
    }

    bool requestedOta = strcmp(otaEnabledParam.getValue(), "1") == 0;
    bool hasOtaHash = media_remote::isValidMd5Hash(previous.otaPasswordHash);
    media_remote::CredentialAction otaAction = media_remote::resolveOtaInput(
      requestedOta, hasOtaHash, otaPasswordParam.getValue(), otaPasswordParam.getValueLength() + 1);
    if (otaAction == media_remote::CredentialAction::Replace) {
      hashOtaPassword(otaPasswordParam.getValue(), proposed.otaPasswordHash);
      proposed.otaEnabled = true;
    } else if (otaAction == media_remote::CredentialAction::Keep) {
      proposed.otaEnabled = true;
    } else if (otaAction == media_remote::CredentialAction::Clear) {
      proposed.otaEnabled = false;
      secureClear(proposed.otaPasswordHash, sizeof(proposed.otaPasswordHash));
    }

    secureClear(const_cast<char *>(tokenParam.getValue()), tokenParam.getValueLength() + 1);
    secureClear(const_cast<char *>(otaPasswordParam.getValue()), otaPasswordParam.getValueLength() + 1);

    config = proposed;
    bool valid = tokenAction != media_remote::CredentialAction::Invalid
      && otaAction != media_remote::CredentialAction::Invalid
      && normalizeAndValidateConfig();
    if (valid) {
      saveConfig();
      secureClear(provisioningPassword, sizeof(provisioningPassword));
      return;
    }

    config = previous;
    tokenParam.setValue("", sizeof(config.token) - 1);
    otaPasswordParam.setValue("", 64);
    drawCenteredMessage("Invalid configuration", "Check token, TLS and OTA");
    Serial.println("Configuration rejected; reopening portal");
    delay(2500);
    requirePortal = true;
  }
}

void schedulePollIn(unsigned long delayMs)
{
  nextPollAt = millis() + delayMs;
}

bool callService(const char *service, const String &body)
{
  HTTPClient http;
  String url = makeHaUrl(String("/api/services/media_player/") + service);
  if (!beginHttp(http, url)) {
    return false;
  }
  addHaHeaders(http);
  int status = http.POST(body);
  Serial.printf("Service %s status: %d\n", service, status);
  http.end();
  return status >= 200 && status < 300;
}

bool callEntityService(const char *service)
{
  return callService(service, "{\"entity_id\":\"" + String(config.entityId) + "\"}");
}

void seekToPosition(long positionMs)
{
  if (currentMedia.durationMs <= 0) {
    return;
  }
  if (positionMs < 0) {
    positionMs = 0;
  }
  if (positionMs > currentMedia.durationMs) {
    positionMs = currentMedia.durationMs;
  }
  callService("media_seek", "{\"entity_id\":\"" + String(config.entityId) +
    "\",\"seek_position\":" + String(positionMs / 1000.0f, 1) + "}");
  currentMedia.progressMs = positionMs;
  if (currentMedia.playing) {
    playbackStartedAt = millis() - positionMs;
  }
  drawProgress(positionMs, currentMedia.durationMs);
  schedulePollIn(800);
}

void setVolume(int volume)
{
  if (volume < 0) {
    volume = 0;
  }
  if (volume > 100) {
    volume = 100;
  }
  currentMedia.volume = volume;
  float level = volume / 100.0f;
  callService("volume_set", "{\"entity_id\":\"" + String(config.entityId) + "\",\"volume_level\":" + String(level, 2) + "}");
  drawVolumeModal();
}

void applyMediaState(JsonVariantConst root);

bool fetchMediaState()
{
  HTTPClient http;
  String url = makeHaUrl(String("/api/states/") + config.entityId);
  if (!beginHttp(http, url)) {
    return false;
  }
  addHaHeaders(http);

  int status = http.GET();
  Serial.printf("GET state %s -> %d\n", url.c_str(), status);
  if (status != 200) {
    Serial.printf("State fetch failed: %d\n", status);
    drawStatusLine("HA state HTTP " + String(status), TFT_RED);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // Spotify entities carry large attributes (source_list with every device),
  // so parse only the keys we actually use.
  StaticJsonDocument<384> filter;
  filter["state"] = true;
  JsonObject filterAttrs = filter.createNestedObject("attributes");
  filterAttrs["media_title"] = true;
  filterAttrs["media_artist"] = true;
  filterAttrs["media_album_name"] = true;
  filterAttrs["entity_picture"] = true;
  filterAttrs["media_duration"] = true;
  filterAttrs["media_position"] = true;
  filterAttrs["media_position_updated_at"] = true;
  filterAttrs["volume_level"] = true;

  DynamicJsonDocument doc(3072);
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    Serial.println("Failed to parse state JSON");
    drawStatusLine("HA JSON parse failed", TFT_RED);
    return false;
  }

  applyMediaState(doc.as<JsonVariantConst>());
  return true;
}

void applyMediaState(JsonVariantConst root)
{
  String previousTrackKey = currentMedia.title + "|" + currentMedia.artist + "|" + currentMedia.album;
  long previousEstimatedProgress = currentMedia.progressMs;
  if (currentMedia.playing && playbackStartedAt > 0) {
    previousEstimatedProgress = millis() - playbackStartedAt;
  }
  bool wasPlaying = currentMedia.playing;

  currentMedia.state = root["state"] | "unknown";
  JsonVariantConst attrs = root["attributes"];
  currentMedia.title = utf8ToAscii(attrs["media_title"] | "");
  currentMedia.artist = utf8ToAscii(attrs["media_artist"] | "");
  currentMedia.album = utf8ToAscii(attrs["media_album_name"] | "");
  currentMedia.picture = attrs["entity_picture"] | "";
  currentMedia.durationMs = (long)((attrs["media_duration"] | 0.0) * 1000.0);
  long haProgressMs = (long)((attrs["media_position"] | 0.0) * 1000.0);
  const char *positionUpdatedAt = attrs["media_position_updated_at"] | "";
  currentMedia.playing = currentMedia.state == "playing";
  currentMedia.available = currentMedia.state != "unavailable" && currentMedia.state != "unknown";
  Serial.printf("HA state=%s title=%s\n", currentMedia.state.c_str(), currentMedia.title.c_str());

  // media_position is a snapshot taken at media_position_updated_at, so
  // while playing it has to be extrapolated to the current time.
  String newTrackKey = currentMedia.title + "|" + currentMedia.artist + "|" + currentMedia.album;
  bool sameTrack = previousTrackKey == newTrackKey;
  bool extrapolated = false;
  if (currentMedia.playing && positionUpdatedAt[0] && clockSynced()) {
    long long updatedAtMs = parseIso8601Ms(positionUpdatedAt);
    if (updatedAtMs > 0) {
      long long elapsed = nowEpochMs() - updatedAtMs;
      if (elapsed > 0) {
        haProgressMs += (long)elapsed;
      }
      extrapolated = true;
    }
  }
  if (!extrapolated && currentMedia.playing && sameTrack && wasPlaying && playbackStartedAt > 0) {
    // No usable wall clock: fall back to the local estimate to avoid the
    // bar jumping backwards on every poll from a stale HA snapshot.
    currentMedia.progressMs = max(haProgressMs, previousEstimatedProgress);
  } else {
    currentMedia.progressMs = haProgressMs;
  }
  if (currentMedia.durationMs > 0 && currentMedia.progressMs > currentMedia.durationMs) {
    currentMedia.progressMs = currentMedia.durationMs;
  }

  if (attrs.containsKey("volume_level")) {
    currentMedia.volume = constrain((int)((attrs["volume_level"] | 0.5) * 100.0), 0, 100);
    currentMedia.hasVolume = true;
  } else {
    currentMedia.hasVolume = false;
  }

  if (currentMedia.playing) {
    playbackStartedAt = millis() - currentMedia.progressMs;
  } else {
    playbackStartedAt = 0;
  }
}

struct JpegStreamCtx {
  WiFiClient *stream;
  HTTPClient *http;
  int remaining;
};

int coverDrawX = 0;
int coverDrawY = 0;

// tjpgd input callback: read (or skip, when buf is null) bytes from the
// HTTP stream, blocking briefly until they arrive.
size_t jpgStreamIn(JDEC *jd, uint8_t *buf, size_t len)
{
  JpegStreamCtx *ctx = (JpegStreamCtx *)jd->device;
  if ((int)len > ctx->remaining) {
    len = ctx->remaining;
  }
  size_t done = 0;
  uint8_t skipBuf[64];
  unsigned long lastByteAt = millis();
  while (done < len && media_remote::elapsedMs(millis(), lastByteAt) < 3000) {
    uint8_t *dst = buf ? buf + done : skipBuf;
    size_t want = buf ? len - done : min(sizeof(skipBuf), len - done);
    int count = ctx->stream->read(dst, want);
    if (count > 0) {
      done += count;
      lastByteAt = millis();
    } else if (!ctx->http->connected() && !ctx->stream->available()) {
      break;
    } else {
      delay(1);
    }
  }
  ctx->remaining -= done;
  return done;
}

int jpgStreamOut(JDEC *jd, void *bitmap, JRECT *rect)
{
  int w = rect->right - rect->left + 1;
  int h = rect->bottom - rect->top + 1;
  tft.pushImage(coverDrawX + rect->left, coverDrawY + rect->top, w, h, (uint16_t *)bitmap);
  return 1;
}

bool downloadAndDrawImage(const String &picture)
{
  if (picture.length() == 0) {
    return false;
  }

  constexpr int maxRedirects = 3;
  String imageUrl = makeHaUrl(picture);
  String visited[maxRedirects + 1];
  HTTPClient clients[maxRedirects + 1];
  HTTPClient *http = nullptr;
  int status = 0;

  for (int hop = 0; hop <= maxRedirects; hop++) {
    visited[hop] = imageUrl;
    http = &clients[hop];
    if (!beginHttp(*http, imageUrl)) {
      return false;
    }
    if (isHaHostUrl(imageUrl)) {
      http->addHeader("Authorization", "Bearer " + String(config.token));
    }

    status = http->GET();
    Serial.printf("GET image hop %d -> %d\n", hop, status);
    if (status == 200) {
      break;
    }

    bool redirect = status == HTTP_CODE_MOVED_PERMANENTLY
      || status == HTTP_CODE_FOUND
      || status == HTTP_CODE_SEE_OTHER
      || status == HTTP_CODE_TEMPORARY_REDIRECT
      || status == HTTP_CODE_PERMANENT_REDIRECT;
    if (!redirect || hop == maxRedirects) {
      Serial.printf("Image fetch failed: %d\n", status);
      http->end();
      return false;
    }

    String location = http->getLocation();
    char resolved[512];
    if (!media_remote::resolveRedirectUrl(imageUrl.c_str(), location.c_str(), resolved, sizeof(resolved))
        || !media_remote::redirectAllowed(imageUrl.c_str(), resolved)) {
      Serial.println("Image redirect blocked");
      drawStatusLine("Image redirect blocked", TFT_RED);
      http->end();
      return false;
    }
    for (int previous = 0; previous <= hop; previous++) {
      if (visited[previous] == resolved) {
        Serial.println("Image redirect loop blocked");
        http->end();
        return false;
      }
    }

    http->end();
    imageUrl = resolved;
  }

  // Decode straight from the HTTP stream with the low-level tjpgd API —
  // needs only a ~4 kB workspace, so cover size doesn't matter.
  JpegStreamCtx ctx;
  ctx.stream = http->getStreamPtr();
  ctx.http = http;
  ctx.remaining = http->getSize() > 0 ? http->getSize() : 0x7FFFFFFF;

  static uint8_t jdWorkspace[3904];
  JDEC jdec;
  JRESULT result = jd_prepare(&jdec, jpgStreamIn, jdWorkspace, sizeof(jdWorkspace), &ctx);
  if (result != JDR_OK) {
    Serial.printf("Cover JPEG prepare failed: %d\n", (int)result);
    http->end();
    return false;
  }
  // Bodmer's tjpgd fork swaps RGB565 bytes inside the decoder; jd_prepare
  // leaves the field uninitialized, so set it explicitly.
  jdec.swap = true;

  uint8_t scale = 0;
  while (scale < 3 && ((jdec.width >> scale) > COVER_SIZE || (jdec.height >> scale) > COVER_SIZE)) {
    scale++;
  }
  int drawW = jdec.width >> scale;
  int drawH = jdec.height >> scale;
  coverDrawX = COVER_X + max(0, (COVER_SIZE - drawW) / 2);
  coverDrawY = COVER_Y + max(0, (COVER_SIZE - drawH) / 2);

  tft.fillRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE, TFT_BLACK);
  result = jd_decomp(&jdec, jpgStreamOut, scale);
  http->end();

  Serial.printf("Cover: %ux%u scale=1/%d result=%d\n", jdec.width, jdec.height, 1 << scale, (int)result);
  if (result != JDR_OK) {
    return false;
  }
  imageDisplayed = true;
  return true;
}

void refreshDisplayAfterState()
{
  if (!currentMedia.available) {
    drawCenteredMessage("Player unavailable", currentMedia.state);
    return;
  }

  if (!baseUiDrawn) {
    drawBaseInterface();
  }

  drawStatusLine(currentMedia.state == "playing" ? "Playing" : currentMedia.state);
  drawText();
  drawPlayIcon(currentMedia.playing);
  drawProgress(currentMedia.progressMs, currentMedia.durationMs);

  if (currentMedia.picture != lastPicture) {
    static String failedPicture;
    static int failCount = 0;
    if (currentMedia.picture != failedPicture) {
      failCount = 0;
    }
    if (failCount >= 3) {
      return; // give up on this picture, keep whatever is on screen
    }
    lastPicture = currentMedia.picture;
    if (!downloadAndDrawImage(currentMedia.picture)) {
      tft.fillRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE, TFT_BLACK);
      tft.drawRect(COVER_X - 1, COVER_Y - 1, COVER_SIZE + 2, COVER_SIZE + 2, TFT_DARKGREY);
      imageDisplayed = false;
      failedPicture = currentMedia.picture;
      failCount++;
      lastPicture = ""; // retry on the next poll (up to 3 attempts)
    } else {
      failCount = 0;
    }
  }
}

// --- Home Assistant WebSocket API ------------------------------------------
// Flow: server sends auth_required -> we send auth -> auth_ok ->
// subscribe_trigger (server-side filtered to our entity, fires on state and
// attribute changes) -> event messages carry the new state in
// event.variables.trigger.to_state.

void wsSendText(const String &text)
{
  ws.sendTXT(text.c_str(), text.length());
}

void wsHandleMessage(uint8_t *payload, size_t length)
{
  StaticJsonDocument<512> filter;
  filter["type"] = true;
  filter["id"] = true;
  filter["success"] = true;
  JsonObject toState = filter["event"]["variables"]["trigger"].createNestedObject("to_state");
  toState["state"] = true;
  JsonObject toAttrs = toState.createNestedObject("attributes");
  toAttrs["media_title"] = true;
  toAttrs["media_artist"] = true;
  toAttrs["media_album_name"] = true;
  toAttrs["entity_picture"] = true;
  toAttrs["media_duration"] = true;
  toAttrs["media_position"] = true;
  toAttrs["media_position_updated_at"] = true;
  toAttrs["volume_level"] = true;

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, payload, length, DeserializationOption::Filter(filter))) {
    Serial.println("WS JSON parse failed");
    return;
  }

  const char *type = doc["type"] | "";

  if (strcmp(type, "auth_required") == 0) {
    wsSendText(String("{\"type\":\"auth\",\"access_token\":\"") + config.token + "\"}");
  } else if (strcmp(type, "auth_ok") == 0) {
    wsSubscriptionId = wsNextMsgId++;
    // A plain state trigger ignores attribute-only changes (track change
    // keeps state "playing"), so subscribe to the relevant attributes too.
    String entity = String("\"entity_id\":\"") + config.entityId + "\"";
    wsSendText(String("{\"id\":") + wsSubscriptionId +
      ",\"type\":\"subscribe_trigger\",\"trigger\":[" +
      "{\"platform\":\"state\"," + entity + "}," +
      "{\"platform\":\"state\"," + entity + ",\"attribute\":\"media_title\"}," +
      "{\"platform\":\"state\"," + entity + ",\"attribute\":\"media_position\"}," +
      "{\"platform\":\"state\"," + entity + ",\"attribute\":\"volume_level\"}]}");
  } else if (strcmp(type, "auth_invalid") == 0) {
    Serial.println("WS auth invalid");
    drawStatusLine("HA auth invalid", TFT_RED);
  } else if (strcmp(type, "result") == 0) {
    if ((doc["id"] | 0UL) == wsSubscriptionId && (doc["success"] | false)) {
      wsSubscribed = true;
      Serial.println("WS subscribed");
    }
  } else if (strcmp(type, "event") == 0) {
    JsonVariantConst state = doc["event"]["variables"]["trigger"]["to_state"];
    if (state.isNull()) {
      return;
    }
    applyMediaState(state);
    if (!showVolumeModal) {
      refreshDisplayAfterState();
    }
  }
}

void wsEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("WS connected");
      wsConnected = true;
      break;
    case WStype_DISCONNECTED:
      if (wsConnected) {
        Serial.println("WS disconnected");
      }
      wsConnected = false;
      wsSubscribed = false;
      // fall back to fast polling right away
      nextPollAt = 0;
      break;
    case WStype_TEXT:
      wsHandleMessage(payload, length);
      break;
    default:
      break;
  }
}

void setupWebSocket()
{
  String base = haBaseUrl();
  media_remote::Origin origin = media_remote::parseHttpOrigin(base.c_str());
  if (!origin.valid) {
    drawStatusLine("Invalid HA URL", TFT_RED);
    return;
  }
  bool tls = origin.scheme == media_remote::UrlScheme::Https;

  Serial.printf("WS connecting to %s:%u (tls=%d)\n", origin.host, origin.port, tls);
  ws.onEvent(wsEvent);
  ws.setReconnectInterval(5000);
  ws.enableHeartbeat(15000, 3000, 2);
  if (tls) {
    if (!config.tlsFingerprint[0]) {
      drawStatusLine("TLS fingerprint missing", TFT_RED);
      return;
    }
    ws.beginSSL(origin.host, origin.port, "/api/websocket", config.tlsFingerprint);
  } else {
    ws.begin(origin.host, origin.port, "/api/websocket");
  }
}

void updateEstimatedProgress()
{
  if (!currentMedia.playing || playbackStartedAt == 0
      || !media_remote::deadlineReached(millis(), nextProgressAt)) {
    return;
  }
  long progress = millis() - playbackStartedAt;
  drawProgress(progress, currentMedia.durationMs);
  nextProgressAt = millis() + progressIntervalMs;
}

void handleTouch()
{
#if defined(PANEL_CYD_2432S028R)
  bool isTouched = touch.touched();
  if (!isTouched) {
    touchWasDown = false;
    return;
  }

  CYD28_TS_Point point = touch.getPointScaled();
  int x = point.x;
  int y = point.y;
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
    touch.isrWake = false;
    return;
  }
#elif defined(PANEL_ESP32_3248S035C)
  TouchPoint point = readGt911Touch();
  if (!point.touched) {
    touchWasDown = false;
    return;
  }
  int x = point.x;
  int y = point.y;
#else
  return;
#endif

  if (touchWasDown) {
    // auto-repeat volume +/- while the finger stays down
    if (showVolumeModal && y > VOLUME_TOP_Y && millis() - lastTouchAt > 350) {
      lastTouchAt = millis();
      volumeModalOpenedAt = millis();
      if (x < (SCREEN_W / 3)) {
        setVolume(currentMedia.volume - 5);
      } else if (x > ((SCREEN_W * 2) / 3)) {
        setVolume(currentMedia.volume + 5);
      }
    }
    return;
  }
  if (millis() - lastTouchAt < touchDebounceMs) {
    return;
  }

  touchWasDown = true;
  lastTouchAt = millis();
  Serial.printf("Touch x=%d y=%d\n", x, y);

  if (showVolumeModal) {
    if (y > VOLUME_TOP_Y) {
      if (x < (SCREEN_W / 3)) {
        setVolume(currentMedia.volume - 5);
      } else if (x > ((SCREEN_W * 2) / 3)) {
        setVolume(currentMedia.volume + 5);
      }
      volumeModalOpenedAt = millis();
    }
    return;
  }

  if (!showVolumeModal && y >= BAR_Y - 14 && y < CONTROL_TOP_Y) {
    seekToPosition((long)((float)(x - BAR_X) / BAR_W * currentMedia.durationMs));
    return;
  }

  if (y > CONTROL_TOP_Y) {
    if (x < (SCREEN_W / 4)) {
      callEntityService("media_previous_track");
      schedulePollIn(800);
    } else if (x < (SCREEN_W / 2)) {
      callEntityService("media_play_pause");
      currentMedia.playing = !currentMedia.playing;
      drawPlayIcon(currentMedia.playing);
      // Give HA time to reflect the new state, otherwise the immediate
      // poll returns the old one and the icon flips back.
      schedulePollIn(1200);
    } else if (x < ((SCREEN_W * 3) / 4)) {
      callEntityService("media_next_track");
      schedulePollIn(800);
    } else if (currentMedia.hasVolume) {
      showVolumeModal = true;
      volumeModalOpenedAt = millis();
      drawVolumeModal();
    }
  }
}

void setup()
{
  Serial.begin(115200);
  ledcSetup(backlightPwmChannel, 5000, 8);
  ledcAttachPin(TFT_BL, backlightPwmChannel);
  ledcWrite(backlightPwmChannel, 255);
  backlightLevel = 255;
  pinMode(0, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.setTextWrap(false);
  TJpgDec.setCallback(tftOutput);
  TJpgDec.setSwapBytes(true);

#if defined(PANEL_CYD_2432S028R)
  touch.begin();
  touch.setRotation(1);
#elif defined(PANEL_ESP32_3248S035C)
  setupGt911();
#endif

  drawCenteredMessage("Starting", "Home Assistant remote");

  // totalBytes()==0 means a corrupt superblock (mounts fine, but the block
  // allocator divides by zero on the first large write) — reformat.
  bool fsOk = LittleFS.begin(false) && LittleFS.totalBytes() > 0;
  if (!fsOk) {
    LittleFS.end();
    Serial.println("Formatting filesystem...");
    LittleFS.format();
    fsOk = LittleFS.begin(false) && LittleFS.totalBytes() > 0;
  }
  if (!fsOk) {
    drawCenteredMessage("Filesystem failed");
    while (true) {
      delay(1000);
    }
  }

  bool haveConfig = loadConfig();
  bool forceConfig = digitalRead(0) == LOW || !haveConfig;
  setupWiFiAndConfig(forceConfig);

  // UTC only — parseIso8601Ms relies on mktime() treating struct tm as UTC.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  if (config.otaEnabled && media_remote::isValidMd5Hash(config.otaPasswordHash)) {
    // Unique per device so two boards on one network don't collide in mDNS.
    char otaHostname[32];
    snprintf(otaHostname, sizeof(otaHostname), "cyd-ha-media-%04x",
      (unsigned)(ESP.getEfuseMac() & 0xFFFF));
    ArduinoOTA.setHostname(otaHostname);
    ArduinoOTA.setPasswordHash(config.otaPasswordHash);
    ArduinoOTA.onStart([]() {
      drawCenteredMessage("OTA update", "Uploading...");
    });
    ArduinoOTA.begin();
    otaReady = true;
    Serial.println("OTA ready");
  } else {
    otaReady = false;
    Serial.println("OTA disabled");
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("HA URL: ");
  Serial.println(config.haUrl);
  Serial.print("Entity: ");
  Serial.println(config.entityId);

  drawBaseInterface();
  drawStatusLine(otaReady ? "OTA ready" : "OTA disabled", otaReady ? TFT_GREEN : TFT_LIGHTGREY);
  delay(400);
  drawStatusLine("WiFi " + WiFi.localIP().toString(), TFT_GREEN);
  delay(500);
  drawStatusLine("Fetching HA state...");
  if (fetchMediaState()) {
    refreshDisplayAfterState();
  } else {
    drawCenteredMessage("HA unavailable", "Check URL/token/entity");
  }
  nextPollAt = millis() + pollIntervalMs;

  setupWebSocket();
}

void loop()
{
  if (otaReady) {
    ArduinoOTA.handle();
  }
  handleTouch();
  updateBacklight();

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiAttemptAt > 5000) {
      lastWifiAttemptAt = millis();
      wifiWasDown = true;
      drawStatusLine("WiFi reconnecting...", TFT_RED);
      WiFi.reconnect();
    }
    return;
  }
  if (wifiWasDown) {
    wifiWasDown = false;
    drawStatusLine("WiFi " + WiFi.localIP().toString(), TFT_GREEN);
    nextPollAt = 0;
  }

  if (showVolumeModal && millis() - volumeModalOpenedAt > 3000) {
    showVolumeModal = false;
    tft.fillRect(0, VOLUME_TOP_Y, SCREEN_W, SCREEN_H - VOLUME_TOP_Y, TFT_BLACK);
    lastTrackKey = "";
    drawText();
    drawProgress(currentMedia.progressMs, currentMedia.durationMs);
    drawControls();
  }

  ws.loop();

  if (media_remote::deadlineReached(millis(), nextPollAt)) {
    nextPollAt = millis() + (wsActive() ? wsPollIntervalMs : pollIntervalMs);
    if (fetchMediaState() && !showVolumeModal) {
      refreshDisplayAfterState();
    }
  }

  if (!showVolumeModal) {
    updateEstimatedProgress();
  }
}

#endif
