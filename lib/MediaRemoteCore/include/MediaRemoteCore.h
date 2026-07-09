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

} // namespace media_remote
