#pragma once
#include <MediaRemoteCore.h>
#include <cstring>
#include <string>
#include <algorithm>

namespace media_remote {
inline bool equalDnsHost(const char *left, const char *right)
{
  while (*left && *right) {
    char a = *left++, b = *right++;
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    if (a != b) return false;
  }
  return *left == *right;
}

template<class Clock> class OperationBudget {
public:
  OperationBudget(uint32_t started, uint32_t total) : started_(started), total_(total) {}
  uint32_t remaining() const {
    uint32_t spent = elapsedMs(Clock::now(), started_);
    return spent >= total_ ? 0 : total_ - spent;
  }
private:
  uint32_t started_, total_;
};

// Backend owns TLS/socket primitives; these overrides are the actual Client
// dispatch surface used by HTTPClient (including its automatic reconnect).
template<class Backend, class Clock> class BoundedTransport : public Backend {
public:
  using Address = typename Backend::Address;
  void configure(const char *host, const char *pin, uint32_t started, uint32_t total) {
    stop();
    host_ = host ? host : "";
    pin_ = pin ? pin : "";
    requirePin_ = pin != nullptr;
    started_ = started;
    total_ = total;
    timedOut_ = false;
  }
  int connect(const char *host, uint16_t port) override { return connect(host, port, 0); }
  int connect(const char *host, uint16_t port, int32_t) override {
    stop();
    if (!host || !equalDnsHost(host_.c_str(), host) || !ready()) return 0;
    char normalized[65];
    if (requirePin_ && !normalizeSha256Fingerprint(pin_.c_str(), normalized, sizeof(normalized))) return 0;
    OperationBudget<Clock> budget(started_, total_);
    if (!Backend::open(host, port, budget) || !ready()
        || (requirePin_ && !Backend::verifyPeer(pin_.c_str(), host))) {
      stop(); return 0;
    }
    authorized_ = true;
    return 1;
  }
  // A numeric address must never bypass the configured hostname/SNI check.
  int connect(Address address, uint16_t port) override {
    return connect(address.toString().c_str(), port, 0);
  }
  int connect(Address address, uint16_t port, int32_t timeout) override {
    return connect(address.toString().c_str(), port, timeout);
  }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t *data, size_t size) override {
    if (!authorized_ || !ready() || !Backend::rawConnected()) return 0;
    size_t written = 0;
    while (written < size && ready()) {
      int count = Backend::writeSome(data + written, size - written, remaining());
      if (count < 0) { stop(); break; }
      written += static_cast<size_t>(count);
      if (!count) Clock::sleep(1);
    }
    return written;
  }
  int available() override { return authorized_ && ready() ? Backend::rawAvailable() : 0; }
  int read() override {
    uint8_t value;
    return read(&value, 1) == 1 ? value : -1;
  }
  int read(uint8_t *data, size_t size) override {
    return authorized_ && ready() ? Backend::rawRead(data, size) : -1;
  }
  int peek() override { return authorized_ && ready() ? Backend::rawPeek() : -1; }
  uint8_t connected() override { return authorized_ && ready() && Backend::rawConnected(); }
  void stop() override { authorized_ = false; Backend::stop(); }
  uint32_t remaining() const { return OperationBudget<Clock>(started_, total_).remaining(); }
  bool timedOut() const { return timedOut_ || !remaining(); }
private:
  bool ready() {
    uint32_t left = remaining();
    // Stream::timedRead consults its timeout after each virtual read. Setting
    // zero at expiration also interrupts a partial HTTP header line.
    Backend::updateReadTimeout(std::min<uint32_t>(left, 3000));
    if (left) return true;
    timedOut_ = true; stop(); return false;
  }
  std::string host_, pin_;
  uint32_t started_ = 0, total_ = 0;
  bool requirePin_ = true, authorized_ = false, timedOut_ = false;
};
// WebSockets owns a long-lived connection. Bound each connection attempt,
// verify before returning it to the library, then retain its normal heartbeat.
template<class Backend, class Clock>
class WebSocketTransport : public Backend {
public:
  WebSocketTransport(const char *host, const char *pin) : host_(host), pin_(pin ? pin : ""), requirePin_(pin != nullptr) {}
  int connect(const char *host, uint16_t port) override { return connect(host, port, 0); }
  int connect(const char *host, uint16_t port, int32_t) override {
    Backend::stop();
    char normalized[65];
    if (!host || !equalDnsHost(host, host_.c_str())
        || (requirePin_ && !normalizeSha256Fingerprint(pin_.c_str(), normalized, sizeof(normalized)))) return 0;
    OperationBudget<Clock> budget(Clock::now(), 6000);
    if (!Backend::open(host, port, budget) || !budget.remaining() || (requirePin_ && !Backend::verifyPeer(pin_.c_str(), host))) {
      Backend::stop(); return 0;
    }
    Backend::updateReadTimeout(3000);
    return 1;
  }
  int connect(typename Backend::Address address, uint16_t port) override { return connect(address.toString().c_str(), port, 0); }
  int connect(typename Backend::Address address, uint16_t port, int32_t timeout) override { return connect(address.toString().c_str(), port, timeout); }
private:
  std::string host_, pin_;
  bool requirePin_;
};

} // namespace media_remote
