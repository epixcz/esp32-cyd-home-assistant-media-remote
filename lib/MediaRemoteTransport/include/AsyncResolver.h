#pragma once
#include <BoundedTransport.h>
#include <atomic>
#include <cstring>

namespace media_remote {
// Resolver and scheduler must outlive every scheduled callback. A timed-out
// lookup retains its slot until completion; it never exposes a stack pointer.
template<class Address, class Scheduler, class Clock>
class AsyncResolver {
public:
  explicit AsyncResolver(Scheduler &scheduler) : scheduler_(scheduler) {}
  bool resolve(const char *host, Address &address, const OperationBudget<Clock> &budget) {
    if (!host || strlen(host) >= sizeof(host_) || !budget.remaining()) return false;
    if (state_.load(std::memory_order_acquire) == Pending) return false;
    strcpy(host_, host);
    state_.store(Pending, std::memory_order_release);
    if (!scheduler_.start(host_, complete, this)) {
      state_.store(Failed, std::memory_order_release);
      return false;
    }
    while (budget.remaining()) {
      int state = state_.load(std::memory_order_acquire);
      if (state == Succeeded) { address = result_; return true; }
      if (state == Failed) return false;
      Clock::sleep(1);
    }
    return false;
  }
private:
  enum { Idle, Pending, Succeeded, Failed };
  static void complete(const Address *address, void *context) {
    auto *self = static_cast<AsyncResolver *>(context);
    if (address) self->result_ = *address;
    self->state_.store(address ? Succeeded : Failed, std::memory_order_release);
  }
  Scheduler &scheduler_;
  std::atomic<int> state_{Idle};
  char host_[96] = {};
  Address result_{};
};
} // namespace media_remote
