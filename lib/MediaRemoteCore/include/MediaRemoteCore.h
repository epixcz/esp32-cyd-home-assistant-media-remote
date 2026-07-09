#pragma once

#include <stddef.h>
#include <stdint.h>

namespace media_remote {

enum class UrlScheme : uint8_t {
  Invalid,
  Http,
  Https,
};

struct Origin {
  bool valid;
  UrlScheme scheme;
  uint16_t port;
  char host[96];
};

enum class CredentialAction : uint8_t {
  Invalid,
  Keep,
  Replace,
  Clear,
};

enum class InputResult : uint8_t {
  Ok,
  TransportError,
  TooLarge,
  Timeout,
  InvalidType,
  DecodeError,
  JsonError,
};

struct Rect {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
};

struct ClippedBlock {
  bool visible;
  Rect destination;
  int32_t sourceX;
  int32_t sourceY;
};

bool deadlineReached(uint32_t now, uint32_t deadline);
uint32_t elapsedMs(uint32_t now, uint32_t startedAt);
int progressBarWidth(int64_t progressMs, int64_t durationMs, int width);
Origin parseHttpOrigin(const char *url);
bool sameOrigin(const Origin &left, const Origin &right);
bool sameOrigin(const char *leftUrl, const char *rightUrl);
bool normalizeSha256Fingerprint(const char *input, char *output, size_t outputSize);
bool resolveRedirectUrl(const char *currentUrl, const char *location, char *output, size_t outputSize);
bool redirectAllowed(const char *currentUrl, const char *nextUrl);
bool isValidOtaPassword(const char *password, size_t bufferSize);
bool isValidMd5Hash(const char *hash);
CredentialAction resolveHaTokenInput(bool hasExistingToken, const char *newToken);
CredentialAction resolveOtaInput(
  bool enabled, bool hasExistingHash, const char *newPassword, size_t passwordBufferSize);
InputResult checkInputBudget(
  uint32_t now, uint32_t startedAt, uint32_t lastByteAt, size_t bytesRead,
  size_t maxBytes, uint32_t totalTimeoutMs, uint32_t idleTimeoutMs);
bool clipDecodedBlock(
  const Rect &sourceBlock, int32_t destinationOriginX, int32_t destinationOriginY,
  const Rect &viewport, const Rect &screen, ClippedBlock *output);

} // namespace media_remote
