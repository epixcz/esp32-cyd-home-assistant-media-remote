#ifdef ARDUINO
#include <MediaRemoteArduino.h>
#include <AsyncResolver.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>

namespace media_remote {
namespace {
class DnsScheduler {
public:
  using Complete = void (*)(const IPAddress *, void *);
  bool start(const char *host, Complete complete, void *context) {
    host_ = host;
    complete_ = complete;
    context_ = context;
    return tcpip_try_callback(startLookup, this) == ERR_OK;
  }
private:
  static void resolved(const char *, const ip_addr_t *address, void *context) {
    auto *self = static_cast<DnsScheduler *>(context);
    IPAddress result;
    if (address) result = IPAddress(ip_2_ip4(address)->addr);
    self->complete_(address ? &result : nullptr, self->context_);
  }
  static void startLookup(void *context) {
    auto *self = static_cast<DnsScheduler *>(context);
    ip_addr_t address;
    err_t result = dns_gethostbyname(self->host_, &address, resolved, self);
    if (result == ERR_OK) resolved(nullptr, &address, self);
    else if (result != ERR_INPROGRESS) resolved(nullptr, nullptr, self);
  }
  const char *host_ = nullptr;
  Complete complete_ = nullptr;
  void *context_ = nullptr;
};
// Static lifetime is required: lwIP may complete after resolve times out.
DnsScheduler scheduler;
AsyncResolver<IPAddress, DnsScheduler, ArduinoClock> resolver(scheduler);
}
bool resolveWithinBudget(const char *host, IPAddress &address, const OperationBudget<ArduinoClock> &budget) {
  if (!host || !budget.remaining()) return false;
  if (address.fromString(host)) return true;
  return resolver.resolve(host, address, budget);
}
} // namespace media_remote
#endif
