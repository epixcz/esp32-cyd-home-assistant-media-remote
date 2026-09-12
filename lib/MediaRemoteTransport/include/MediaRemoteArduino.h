#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <BoundedTransport.h>
#include <lwip/sockets.h>

namespace media_remote {
struct ArduinoClock {
  static uint32_t now() { return millis(); }
  static void sleep(uint32_t ms) { delay(ms); }
};
bool resolveWithinBudget(const char *host, IPAddress &address, const OperationBudget<ArduinoClock> &budget);

class PlainBackend : public WiFiClient {
public:
  using Address = IPAddress;
  bool open(const char *host, uint16_t port, const OperationBudget<ArduinoClock> &budget) {
    IPAddress address;
    if (!resolveWithinBudget(host, address, budget) || !budget.remaining()) return false;
    return WiFiClient::connect(address, port, static_cast<int32_t>(budget.remaining()));
  }
  bool verifyPeer(const char *, const char *) { return false; }
  void updateReadTimeout(uint32_t ms) { Stream::setTimeout(ms); }
  int writeSome(const uint8_t *data, size_t size, uint32_t) {
    // The stock WiFiClient retries partial writes with a fresh timeout. Use
    // a nonblocking write instead; BoundedTransport owns the shared deadline.
    int result = lwip_send(fd(), data, size, MSG_DONTWAIT);
    return result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : result;
  }
  int rawAvailable() { return WiFiClient::available(); }
  int rawRead(uint8_t *data, size_t size) { return WiFiClient::read(data, size); }
  int rawPeek() { return WiFiClient::peek(); }
  uint8_t rawConnected() { return WiFiClient::connected(); }
};
class SecureBackend : public WiFiClientSecure {
public:
  using Address = IPAddress;
  bool open(const char *host, uint16_t port, const OperationBudget<ArduinoClock> &budget) {
    IPAddress address;
    if (!resolveWithinBudget(host, address, budget)) return false;
    // This API combines TCP and TLS. Divide the remaining allowance between
    // its two independent timeouts; never let zero select a library default.
    uint32_t allowance = budget.remaining() / 2;
    if (!allowance) return false;
    _timeout = allowance;
    sslclient->handshake_timeout = allowance;
    setInsecure(); // verified by the outer adapter before any application bytes
    return WiFiClientSecure::connect(address, port, host, nullptr, nullptr, nullptr);
  }
  bool verifyPeer(const char *pin, const char *host) { return WiFiClientSecure::verify(pin, host); }
  void updateReadTimeout(uint32_t ms) {
    Stream::setTimeout(ms);
    sslclient->socket_timeout = ms;
  }
  int writeSome(const uint8_t *data, size_t size, uint32_t remaining) {
    sslclient->socket_timeout = remaining;
    return WiFiClientSecure::write(data, size);
  }
  int rawAvailable() { return WiFiClientSecure::available(); }
  int rawRead(uint8_t *data, size_t size) { return WiFiClientSecure::read(data, size); }
  int rawPeek() { return WiFiClientSecure::peek(); }
  uint8_t rawConnected() { return WiFiClientSecure::connected(); }
};
using WebSocketPlainTransport = WebSocketTransport<PlainBackend, ArduinoClock>;
using WebSocketSecureTransport = WebSocketTransport<SecureBackend, ArduinoClock>;
using PlainTransport = BoundedTransport<PlainBackend, ArduinoClock>;
using SecureTransport = BoundedTransport<SecureBackend, ArduinoClock>;
} // namespace media_remote
