#include "MediaRemoteCore.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace media_remote {

namespace {

bool startsWithIgnoreCase(const char *value, const char *prefix)
{
  while (*prefix) {
    if (!*value || tolower(static_cast<unsigned char>(*value)) != *prefix) {
      return false;
    }
    value++;
    prefix++;
  }
  return true;
}

bool parsePort(const char *start, const char *end, uint16_t *port)
{
  if (start == end) {
    return false;
  }

  uint32_t value = 0;
  for (const char *cursor = start; cursor < end; cursor++) {
    if (!isdigit(static_cast<unsigned char>(*cursor))) {
      return false;
    }
    value = value * 10 + static_cast<uint32_t>(*cursor - '0');
    if (value > 65535) {
      return false;
    }
  }

  if (value == 0) {
    return false;
  }
  *port = static_cast<uint16_t>(value);
  return true;
}

bool copyHost(const char *start, const char *end, char *destination, size_t size)
{
  size_t length = static_cast<size_t>(end - start);
  if (length == 0 || length >= size) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    unsigned char value = static_cast<unsigned char>(start[i]);
    if (isspace(value) || value == '@' || value == '[' || value == ']') {
      return false;
    }
    destination[i] = static_cast<char>(tolower(value));
  }
  destination[length] = '\0';
  return true;
}

bool appendText(char *output, size_t outputSize, size_t *length, const char *start, size_t count)
{
  if (!output || !length || *length + count >= outputSize) {
    return false;
  }
  memcpy(output + *length, start, count);
  *length += count;
  output[*length] = '\0';
  return true;
}

char hexDigit(unsigned value)
{
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

int hexValue(char value)
{
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  value = static_cast<char>(tolower(static_cast<unsigned char>(value)));
  return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

} // namespace

bool deadlineReached(uint32_t now, uint32_t deadline)
{
  return deadline == 0 || static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t elapsedMs(uint32_t now, uint32_t startedAt)
{
  return now - startedAt;
}

int progressBarWidth(int64_t progressMs, int64_t durationMs, int width)
{
  if (durationMs <= 0 || width <= 0) {
    return 0;
  }
  if (progressMs <= 0) {
    return 0;
  }
  if (progressMs >= durationMs) {
    return width;
  }
  return static_cast<int>((progressMs * width) / durationMs);
}

Origin parseHttpOrigin(const char *url)
{
  Origin result = {false, UrlScheme::Invalid, 0, {0}};
  if (!url) {
    return result;
  }

  const char *authority = nullptr;
  if (startsWithIgnoreCase(url, "http://")) {
    result.scheme = UrlScheme::Http;
    result.port = 80;
    authority = url + 7;
  } else if (startsWithIgnoreCase(url, "https://")) {
    result.scheme = UrlScheme::Https;
    result.port = 443;
    authority = url + 8;
  } else {
    return result;
  }

  const char *authorityEnd = authority;
  while (*authorityEnd && *authorityEnd != '/' && *authorityEnd != '?' && *authorityEnd != '#') {
    authorityEnd++;
  }
  if (authority == authorityEnd) {
    return result;
  }

  for (const char *cursor = authority; cursor < authorityEnd; cursor++) {
    if (*cursor == '@' || isspace(static_cast<unsigned char>(*cursor))) {
      return result;
    }
  }

  const char *hostStart = authority;
  const char *hostEnd = authorityEnd;
  const char *portStart = nullptr;

  if (*authority == '[') {
    const char *closing = authority + 1;
    while (closing < authorityEnd && *closing != ']') {
      closing++;
    }
    if (closing == authorityEnd || closing == authority + 1) {
      return result;
    }
    hostStart = authority + 1;
    hostEnd = closing;
    if (closing + 1 < authorityEnd) {
      if (closing[1] != ':') {
        return result;
      }
      portStart = closing + 2;
    } else if (closing + 1 != authorityEnd) {
      return result;
    }
  } else {
    const char *colon = nullptr;
    for (const char *cursor = authority; cursor < authorityEnd; cursor++) {
      if (*cursor == ':') {
        if (colon) {
          return result;
        }
        colon = cursor;
      }
    }
    if (colon) {
      hostEnd = colon;
      portStart = colon + 1;
    }
  }

  if (!copyHost(hostStart, hostEnd, result.host, sizeof(result.host))) {
    return result;
  }
  if (portStart && !parsePort(portStart, authorityEnd, &result.port)) {
    result.host[0] = '\0';
    return result;
  }

  result.valid = true;
  return result;
}

bool sameOrigin(const Origin &left, const Origin &right)
{
  return left.valid && right.valid
    && left.scheme == right.scheme
    && left.port == right.port
    && strcmp(left.host, right.host) == 0;
}

bool sameOrigin(const char *leftUrl, const char *rightUrl)
{
  return sameOrigin(parseHttpOrigin(leftUrl), parseHttpOrigin(rightUrl));
}

bool normalizeSha256Fingerprint(const char *input, char *output, size_t outputSize)
{
  if (!input || !output || outputSize < 65) {
    return false;
  }

  size_t written = 0;
  int high = -1;
  for (const char *cursor = input; *cursor; cursor++) {
    if (*cursor == ':' || *cursor == ' ') {
      continue;
    }
    int value = hexValue(*cursor);
    if (value < 0 || written >= 64) {
      output[0] = '\0';
      return false;
    }
    if (high < 0) {
      high = value;
    } else {
      output[written++] = hexDigit(static_cast<unsigned>(high));
      output[written++] = hexDigit(static_cast<unsigned>(value));
      high = -1;
    }
  }

  if (written != 64 || high >= 0) {
    output[0] = '\0';
    return false;
  }
  output[written] = '\0';
  return true;
}

bool resolveRedirectUrl(const char *currentUrl, const char *location, char *output, size_t outputSize)
{
  if (!currentUrl || !location || !output || outputSize == 0 || !*location) {
    return false;
  }
  output[0] = '\0';

  Origin currentOrigin = parseHttpOrigin(currentUrl);
  if (!currentOrigin.valid) {
    return false;
  }

  if (parseHttpOrigin(location).valid) {
    size_t length = strlen(location);
    if (length >= outputSize) {
      return false;
    }
    memcpy(output, location, length + 1);
    return true;
  }

  // A Location value that names another URI scheme is not a relative HTTP
  // path. Reject it instead of accidentally keeping it on the current origin.
  if (strstr(location, "://")) {
    return false;
  }

  size_t length = 0;
  const char *scheme = currentOrigin.scheme == UrlScheme::Https ? "https://" : "http://";
  if (location[0] == '/' && location[1] == '/') {
    return appendText(output, outputSize, &length, scheme, strlen(scheme) - 2)
      && appendText(output, outputSize, &length, location, strlen(location));
  }

  if (!appendText(output, outputSize, &length, scheme, strlen(scheme))) {
    return false;
  }
  bool ipv6 = strchr(currentOrigin.host, ':') != nullptr;
  if (ipv6 && !appendText(output, outputSize, &length, "[", 1)) {
    return false;
  }
  if (!appendText(output, outputSize, &length, currentOrigin.host, strlen(currentOrigin.host))) {
    return false;
  }
  if (ipv6 && !appendText(output, outputSize, &length, "]", 1)) {
    return false;
  }
  uint16_t defaultPort = currentOrigin.scheme == UrlScheme::Https ? 443 : 80;
  if (currentOrigin.port != defaultPort) {
    char portText[7];
    int portLength = snprintf(portText, sizeof(portText), ":%u", currentOrigin.port);
    if (portLength <= 0 || !appendText(output, outputSize, &length, portText, static_cast<size_t>(portLength))) {
      return false;
    }
  }

  if (location[0] == '/') {
    return appendText(output, outputSize, &length, location, strlen(location));
  }

  const char *path = strstr(currentUrl, "://");
  path = path ? strchr(path + 3, '/') : nullptr;
  if (!path) {
    if (!appendText(output, outputSize, &length, "/", 1)) {
      return false;
    }
  } else {
    const char *pathEnd = path;
    while (*pathEnd && *pathEnd != '?' && *pathEnd != '#') {
      pathEnd++;
    }
    const char *lastSlash = path;
    for (const char *cursor = path; cursor < pathEnd; cursor++) {
      if (*cursor == '/') {
        lastSlash = cursor;
      }
    }
    if (!appendText(output, outputSize, &length, path, static_cast<size_t>(lastSlash - path + 1))) {
      return false;
    }
  }
  return appendText(output, outputSize, &length, location, strlen(location));
}

bool redirectAllowed(const char *currentUrl, const char *nextUrl)
{
  Origin current = parseHttpOrigin(currentUrl);
  Origin next = parseHttpOrigin(nextUrl);
  if (!current.valid || !next.valid) {
    return false;
  }
  return !(current.scheme == UrlScheme::Https && next.scheme == UrlScheme::Http);
}

bool isValidOtaPassword(const char *password, size_t bufferSize)
{
  if (!password || bufferSize < 2) {
    return false;
  }

  size_t length = 0;
  while (length < bufferSize && password[length]) {
    length++;
  }
  return length >= 12 && length < bufferSize;
}

bool isValidMd5Hash(const char *hash)
{
  if (!hash) {
    return false;
  }
  for (size_t index = 0; index < 32; index++) {
    if (!hash[index] || hexValue(hash[index]) < 0) {
      return false;
    }
  }
  return hash[32] == '\0';
}

CredentialAction resolveHaTokenInput(bool hasExistingToken, const char *newToken)
{
  if (newToken && newToken[0]) {
    return CredentialAction::Replace;
  }
  return hasExistingToken ? CredentialAction::Keep : CredentialAction::Invalid;
}

CredentialAction resolveOtaInput(
  bool enabled, bool hasExistingHash, const char *newPassword, size_t passwordBufferSize)
{
  if (!enabled) {
    return CredentialAction::Clear;
  }
  if (!newPassword || !newPassword[0]) {
    return hasExistingHash ? CredentialAction::Keep : CredentialAction::Invalid;
  }
  return isValidOtaPassword(newPassword, passwordBufferSize)
    ? CredentialAction::Replace
    : CredentialAction::Invalid;
}

} // namespace media_remote
