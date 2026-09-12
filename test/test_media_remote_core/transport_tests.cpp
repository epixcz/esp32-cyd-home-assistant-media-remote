#include <unity.h>
#include <HttpBodyStream.h>
#include <string>
#include <cstring>

struct Clock {
  static uint32_t tick;
  static uint32_t now() { return tick; }
  static void sleep(uint32_t ms) { tick += ms; }
};
uint32_t Clock::tick = 0;
struct StreamBase {
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual size_t write(uint8_t) = 0;
  virtual size_t readBytes(char *, size_t) = 0;
  virtual size_t readBytes(uint8_t *, size_t) = 0;
};
struct Wire {
  std::string bytes;
  size_t pos = 0;
  size_t fragment = 2;
  bool keepOpen = false;
  uint32_t interval = 0, nextByteAt = 0;
  explicit Wire(const char *value) : bytes(value) {}
  int available() { return Clock::tick >= nextByteAt ? std::min(fragment, bytes.size() - pos) : 0; }
  int read() { return available() ? bytes[pos++] : -1; }
  int read(uint8_t *out, size_t size) {
    size = std::min(size, static_cast<size_t>(available()));
    memcpy(out, bytes.data() + pos, size); pos += size;
    if (size) nextByteAt = Clock::tick + interval;
    return size;
  }
  bool connected() { return keepOpen || pos < bytes.size(); }
  void flush() {}
};
using Body = media_remote::HttpBodyStream<StreamBase, Wire, Wire, Clock>;
void testActualFragmentedBody() {
  Clock::tick = 0;
  Wire wire("hello"); Body body(&wire, &wire, false, 5, 5, 100, 20, 0);
  char result[10] = {};
  TEST_ASSERT_EQUAL(5, body.readBytes(result, 10));
  TEST_ASSERT_EQUAL_STRING("hello", result);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(body.result()));
}
void testActualChunkedBody() {
  Clock::tick = 0;
  Wire wire("2\r\nhe\r\n3;ext=yes\r\nllo\r\n0\r\n\r\n");
  Body body(&wire, &wire, true, -1, 5, 100, 20, 0);
  char result[10] = {};
  TEST_ASSERT_EQUAL(5, body.readBytes(result, 10));
  TEST_ASSERT_EQUAL_STRING("hello", result);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(body.result()));
}
void testTruncatedAndMalformedBodies() {
  Clock::tick = 0;
  Wire shortWire("ab"); Body shortBody(&shortWire, &shortWire, false, 3, 10, 100, 20, 0);
  char result[10]; shortBody.readBytes(result, 10);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(media_remote::InputResult::TransportError), static_cast<int>(shortBody.result()));
  Wire bad("z\r\n"); Body malformed(&bad, &bad, true, -1, 10, 100, 20, 0);
  malformed.readBytes(result, 10);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(media_remote::InputResult::TransportError), static_cast<int>(malformed.result()));
}
void testBodyIdleDeadline() {
  Clock::tick = 0;
  Wire wire(""); wire.keepOpen = true;
  Body body(&wire, &wire, false, 3, 10, 100, 20, 0);
  char result[10]; body.readBytes(result, 10);
  TEST_ASSERT_EQUAL_UINT32(20, Clock::tick);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(media_remote::InputResult::Timeout), static_cast<int>(body.result()));
}
void runTransportTests() {
  RUN_TEST(testActualFragmentedBody);
  RUN_TEST(testActualChunkedBody);
  RUN_TEST(testTruncatedAndMalformedBodies);
  RUN_TEST(testBodyIdleDeadline);
}

#include <BoundedTransport.h>
#include <ConfirmedControls.h>
struct Address {
  std::string value;
  std::string toString() const { return value; }
};
struct ClientApi {
  virtual int connect(const char *, uint16_t) = 0;
  virtual int connect(const char *, uint16_t, int32_t) = 0;
  virtual int connect(Address, uint16_t) = 0;
  virtual int connect(Address, uint16_t, int32_t) = 0;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *, size_t) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int read(uint8_t *, size_t) = 0;
  virtual int peek() = 0;
  virtual uint8_t connected() = 0;
  virtual void stop() = 0;
};
struct Backend : ClientApi {
  using Address = ::Address;
  bool socket = false, validPin = true;
  int opens = 0;
  uint32_t dnsDelay = 0, tcpDelay = 0, tlsDelay = 0, writeDelay = 0;
  uint32_t timeout = 0, readInterval = 0;
  std::string sent;
  bool phase(uint32_t delay, const media_remote::OperationBudget<Clock> &budget) {
    Clock::tick += std::min(delay, budget.remaining());
    return budget.remaining() > 0;
  }
  bool open(const char *, uint16_t, const media_remote::OperationBudget<Clock> &budget) {
    opens++;
    socket = phase(dnsDelay, budget) && phase(tcpDelay, budget) && phase(tlsDelay, budget);
    return socket;
  }
  bool verifyPeer(const char *, const char *) { return validPin; }
  void updateReadTimeout(uint32_t ms) { timeout = ms; }
  int writeSome(const uint8_t *data, size_t size, uint32_t remaining) {
    Clock::tick += std::min(writeDelay, remaining);
    if (writeDelay >= remaining) return 0;
    // Force partial writes to prove the budget is not reset per piece.
    size = std::min<size_t>(size, 1);
    sent.append(reinterpret_cast<const char *>(data), size); return size;
  }
  int rawAvailable() { return 0; }
  int rawRead(uint8_t *data, size_t size) {
    Clock::sleep(1);
    if (size && readInterval && Clock::tick % readInterval == 0) { *data = 'x'; return 1; }
    return -1;
  }
  int rawPeek() { return -1; }
  uint8_t rawConnected() { return socket; }
  void stop() override { socket = false; }
};
using Transport = media_remote::BoundedTransport<Backend, Clock>;
const char *pin = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
bool request(ClientApi &client, const char *host) {
  if (!client.connected() && !client.connect(host, 443, 2000)) return false;
  const char *header = "Authorization: Bearer fixture\r\n";
  return client.write(reinterpret_cast<const uint8_t *>(header), strlen(header)) == strlen(header);
}
void testPerConnectPinAndHost() {
  Clock::tick = 0;
  Transport client;
  client.configure("ha.test", pin, 0, 100);
  TEST_ASSERT_TRUE(request(client, "HA.TEST"));
  TEST_ASSERT_EQUAL(1, client.opens);
  client.sent.clear(); client.socket = false; client.validPin = false;
  TEST_ASSERT_FALSE(request(client, "ha.test"));
  TEST_ASSERT_EQUAL(2, client.opens);
  TEST_ASSERT_TRUE(client.sent.empty());
  client.configure("ha.test", "", 0, 100);
  TEST_ASSERT_FALSE(request(client, "ha.test"));
  client.configure("ha.test", pin, 0, 100);
  TEST_ASSERT_FALSE(request(client, "evil.test"));
  ClientApi &api = client;
  TEST_ASSERT_FALSE(api.connect(Address{"1.2.3.4"}, 443));
  TEST_ASSERT_FALSE(api.connect(Address{"1.2.3.4"}, 443, 2000));
  TEST_ASSERT_EQUAL(2, client.opens);
  TEST_ASSERT_TRUE(client.sent.empty());
}
void testConnectAndWriteBudgets() {
  for (int phase = 0; phase < 4; phase++) {
    Clock::tick = 0;
    Transport client; client.configure("ha.test", pin, 0, 10);
    if (phase == 0) client.dnsDelay = 50;
    if (phase == 1) client.tcpDelay = 50;
    if (phase == 2) client.tlsDelay = 50;
    if (phase == 3) client.writeDelay = 2;
    TEST_ASSERT_FALSE(request(client, "ha.test"));
    TEST_ASSERT_TRUE(client.timedOut());
    TEST_ASSERT_FALSE(client.socket);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(11, Clock::tick);
  }
}
void testSharedBudgetAndRollover() {
  Clock::tick = UINT32_MAX - 5;
  uint32_t started = Clock::tick;
  Transport client; client.configure("ha.test", pin, started, 10);
  client.tcpDelay = 4;
  TEST_ASSERT_TRUE(client.connect("ha.test", 443));
  client.configure("cover.test", nullptr, started, 10);
  client.tcpDelay = 7;
  TEST_ASSERT_FALSE(client.connect("cover.test", 443));
  TEST_ASSERT_EQUAL_UINT32(10, static_cast<uint32_t>(Clock::tick - started));
}
void testPartialHeaderStreamTimeout() {
  Clock::tick = 0;
  Transport client; client.configure("ha.test", pin, 0, 10);
  TEST_ASSERT_TRUE(client.connect("ha.test", 443));
  // Same timedRead loop as pinned Arduino Stream.cpp: it rereads the timeout
  // following virtual read(), which must set zero when the deadline expires.
  uint32_t started = Clock::now();
  int value;
  do { value = client.read(); if (value >= 0) break; }
  while (Clock::now() - started < client.timeout);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(10, Clock::tick);
}
struct State { bool playing = false; long durationMs = 10000, progressMs = 2000; int volume = 50; };
void testConfirmedControls() {
  State state; unsigned long anchor = 0;
  int calls = 0;
  for (int status : {400, 500, -1, -11}) {
    TEST_ASSERT_FALSE(media_remote::confirmPlayPause(state, anchor, 200, [&]() { calls++; return status >= 200 && status < 300; }));
    TEST_ASSERT_FALSE(media_remote::confirmSeek(state, anchor, 5000, 200, [&](long) { return false; }));
    TEST_ASSERT_FALSE(media_remote::confirmVolume(state, 55, [&](int requested) { TEST_ASSERT_EQUAL(55, requested); return false; }));
    TEST_ASSERT_FALSE(state.playing); TEST_ASSERT_EQUAL(2000, state.progressMs); TEST_ASSERT_EQUAL(50, state.volume);
  }
  TEST_ASSERT_EQUAL(4, calls);
  TEST_ASSERT_TRUE(media_remote::confirmPlayPause(state, anchor, 3000, [&]() { calls++; return true; }));
  TEST_ASSERT_EQUAL(1000, anchor);
  TEST_ASSERT_TRUE(media_remote::confirmPlayPause(state, anchor, 4000, []() { return true; }));
  TEST_ASSERT_EQUAL(3000, state.progressMs);
  TEST_ASSERT_TRUE(media_remote::confirmPlayPause(state, anchor, 9000, []() { return true; }));
  TEST_ASSERT_EQUAL(6000, anchor); // 5 seconds paused were not counted
  TEST_ASSERT_TRUE(media_remote::confirmVolume(state, 100, [](int value) { return value == 100; }));
  TEST_ASSERT_FALSE(media_remote::confirmVolume(state, 105, [&](int) { calls++; return true; }));
  TEST_ASSERT_EQUAL(5, calls);
}
void runAdapterTests() {
  RUN_TEST(testPerConnectPinAndHost);
  RUN_TEST(testConnectAndWriteBudgets);
  RUN_TEST(testSharedBudgetAndRollover);
  RUN_TEST(testPartialHeaderStreamTimeout);
  RUN_TEST(testConfirmedControls);
}

struct SocketBackend : Backend {
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t *data, size_t size) override {
    if (!socket) return 0;
    sent.append(reinterpret_cast<const char *>(data), size); return size;
  }
  int available() override { return rawAvailable(); }
  int read() override { return -1; }
  int read(uint8_t *data, size_t size) override { return rawRead(data, size); }
  int peek() override { return -1; }
  uint8_t connected() override { return rawConnected(); }
  bool verify(const char *value, const char *host) { return verifyPeer(value, host); }
};
using WebSocketTransport = media_remote::WebSocketTransport<SocketBackend, Clock>;
#define HAS_SSL
#define ESP32
#define SSL_FINGERPRINT_IS_SET (_fingerprint.length())
#define DEBUG_WEBSOCKETS(...)
struct WebSockets {
  template<class Client> static void clientDisconnect(Client *client, int) { client->ssl->stop(); }
};
struct Upgrade {
  struct Client { bool isSSL = true; WebSocketTransport *ssl; } _client;
  std::string _fingerprint = pin, _host = "ha.test";
  const char *_CA_cert = nullptr;
  int upgradeCount = 0;
  explicit Upgrade(WebSocketTransport &transport) { _client.ssl = &transport; }
  void sendHeader(Client *) { upgradeCount++; }
  void connectedCb() {
#include "../fixtures/websockets_connected_pin.inc"
  }
};
#undef DEBUG_WEBSOCKETS
#undef SSL_FINGERPRINT_IS_SET
#undef ESP32
#undef HAS_SSL
void testActualWebSocketConnectionAndUpgrade() {
  for (int scenario = 0; scenario < 4; scenario++) {
    Clock::tick = 0;
    WebSocketTransport client("ha.test", scenario == 1 ? "" : pin);
    client.validPin = scenario != 2;
    Upgrade upgrade(client);
    ClientApi &api = client;
    if (api.connect(scenario == 3 ? "evil.test" : "HA.TEST", 443)) upgrade.connectedCb();
    TEST_ASSERT_EQUAL(scenario == 0 ? 1 : 0, upgrade.upgradeCount);
    TEST_ASSERT_EQUAL(scenario == 0 ? 1 : 0, client.socket);
  }
  Clock::tick = 0;
  WebSocketTransport client("ha.test", pin);
  Upgrade upgrade(client);
  TEST_ASSERT_TRUE(client.connect("ha.test", 443));
  // The pinned library callback must also reject a failed verification before
  // upgrade bytes even if a future connection adapter regresses.
  client.validPin = false; upgrade.connectedCb();
  TEST_ASSERT_EQUAL(0, upgrade.upgradeCount);
  TEST_ASSERT_FALSE(client.socket);
}
void testWebSocketConnectBudget() {
  Clock::tick = 0;
  WebSocketTransport client("ha.test", pin);
  client.dnsDelay = 4000; client.tlsDelay = 4000;
  TEST_ASSERT_FALSE(client.connect("ha.test", 443));
  TEST_ASSERT_EQUAL_UINT32(6000, Clock::tick);
  TEST_ASSERT_FALSE(client.socket);
}
void runWebSocketTests() {
  RUN_TEST(testActualWebSocketConnectionAndUpgrade);
  RUN_TEST(testWebSocketConnectBudget);
}

#include <AsyncResolver.h>
struct Scheduler {
  using Complete = void (*)(const int *, void *);
  Complete completion = nullptr;
  void *context = nullptr;
  const char *host = nullptr;
  int calls = 0;
  bool queued = true, immediate = false;
  bool start(const char *value, Complete callback, void *data) {
    calls++; host = value; completion = callback; context = data;
    if (immediate) finish(42);
    return queued;
  }
  void finish(int address) { completion(&address, context); }
};
void testResolverTimeoutAndLateCompletion() {
  Clock::tick = 0;
  Scheduler scheduler;
  media_remote::AsyncResolver<int, Scheduler, Clock> resolver(scheduler);
  int address = 0;
  TEST_ASSERT_FALSE(resolver.resolve("first.test", address, {0, 10}));
  TEST_ASSERT_EQUAL_UINT32(10, Clock::tick);
  TEST_ASSERT_EQUAL(0, address);
  TEST_ASSERT_FALSE(resolver.resolve("second.test", address, {10, 10}));
  TEST_ASSERT_EQUAL(1, scheduler.calls);
  TEST_ASSERT_EQUAL_STRING("first.test", scheduler.host);
  scheduler.finish(99); // late callback still has its original live context
  scheduler.immediate = true;
  TEST_ASSERT_TRUE(resolver.resolve("second.test", address, {10, 10}));
  TEST_ASSERT_EQUAL(42, address);
  TEST_ASSERT_EQUAL(2, scheduler.calls);
  scheduler.queued = false; scheduler.immediate = false;
  TEST_ASSERT_FALSE(resolver.resolve("third.test", address, {10, 10}));
  TEST_ASSERT_EQUAL(42, address);
}
void testBodyTotalBudgetAndExactCeiling() {
  Clock::tick = 90;
  Wire wire(""); wire.keepOpen = true;
  Body body(&wire, &wire, false, -1, 5, 100, 20, 0);
  char buffer[10]; body.readBytes(buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT32(100, Clock::tick);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(media_remote::InputResult::Timeout), static_cast<int>(body.result()));
  Clock::tick = 0;
  Wire exact("12345"); Body exactBody(&exact, &exact, false, -1, 5, 100, 20, 0);
  TEST_ASSERT_EQUAL(5, exactBody.readBytes(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(exactBody.result()));
  Wire over("123456"); Body overBody(&over, &over, false, -1, 5, 100, 20, 0);
  TEST_ASSERT_EQUAL(5, overBody.readBytes(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(media_remote::InputResult::TooLarge), static_cast<int>(overBody.result()));
}
void runResolverTests() {
  RUN_TEST(testResolverTimeoutAndLateCompletion);
  RUN_TEST(testBodyTotalBudgetAndExactCeiling);
}

#include <HttpAuthorization.h>
void testProductionAuthorizationBoundary() {
  struct Headers {
    int calls = 0;
    void addHeader(const char *, const char *) { calls++; }
  } headers;
  media_remote::addHaAuthorization(headers, "https://HA.test", "https://ha.test/api/states/x", "fixture");
  TEST_ASSERT_EQUAL(1, headers.calls);
  for (const char *url : {"https://cover.test/x", "https://ha.test:444/x", "http://ha.test/x", "https://ha.test.evil.test/x"}) {
    media_remote::addHaAuthorization(headers, "https://ha.test", url, "fixture");
  }
  TEST_ASSERT_EQUAL(1, headers.calls);
}
void runAuthorizationTests() { RUN_TEST(testProductionAuthorizationBoundary); }

void testTricklingBodyAndHeaderStopAtTotalDeadline() {
  Clock::tick = 0;
  Wire wire("abcdefghijklmnopqrstuv"); wire.interval = 2; wire.fragment = 1;
  Body body(&wire, &wire, false, 22, 100, 10, 3, 0);
  char output[30];
  TEST_ASSERT_EQUAL(5, body.readBytes(output, sizeof(output)));
  TEST_ASSERT_EQUAL_UINT32(10, Clock::tick);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(media_remote::InputResult::Timeout), static_cast<int>(body.result()));
  Clock::tick = 0;
  Transport client; client.configure("ha.test", pin, 0, 10); client.readInterval = 2;
  TEST_ASSERT_TRUE(client.connect("ha.test", 443));
  int bytes = 0;
  while (true) {
    uint32_t started = Clock::now();
    int value;
    do { value = client.read(); if (value >= 0) break; }
    while (Clock::now() - started < client.timeout);
    if (value < 0) break;
    bytes++;
  }
  TEST_ASSERT_EQUAL(5, bytes);
  TEST_ASSERT_EQUAL_UINT32(10, Clock::tick);
  TEST_ASSERT_FALSE(client.socket);
}
void testPlainWebSocketConnectionBudget() {
  Clock::tick = 0;
  WebSocketTransport client("ha.test", nullptr);
  client.validPin = false;
  TEST_ASSERT_TRUE(client.connect("ha.test", 80));
  client.socket = false; client.tcpDelay = 7000;
  TEST_ASSERT_FALSE(client.connect("ha.test", 80));
  TEST_ASSERT_EQUAL_UINT32(6000, Clock::tick);
}
void testChunkFramingPreservesTimeout() {
  Clock::tick = 0;
  Wire wire("1\r\na"); wire.keepOpen = true;
  Body body(&wire, &wire, true, -1, 100, 10, 3, 0);
  char output[10];
  TEST_ASSERT_EQUAL(1, body.readBytes(output, sizeof(output)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(media_remote::InputResult::Timeout), static_cast<int>(body.result()));
}
void runTrickleTests() {
  RUN_TEST(testTricklingBodyAndHeaderStopAtTotalDeadline);
  RUN_TEST(testPlainWebSocketConnectionBudget);
  RUN_TEST(testChunkFramingPreservesTimeout);
}
