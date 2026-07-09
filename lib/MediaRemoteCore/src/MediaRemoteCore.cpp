#include "MediaRemoteCore.h"

#include <ctype.h>
#include <stddef.h>
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

} // namespace media_remote
