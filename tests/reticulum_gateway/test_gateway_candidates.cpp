#include "platform/esp/arduino_common/chat/infra/reticulum/gateway_candidates.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

using chat::reticulum::GatewayCandidates;
using chat::reticulum::TcpRetry;

int main()
{
    GatewayCandidates pool;
    GatewayCandidates::Endpoint old{}, fresh{};
    std::strcpy(old.host, "stable.example");
    old.port = 4242;
    std::strcpy(fresh.host, "new.example");
    fresh.port = 4242;
    auto allow = [](const auto&)
    { return true; };
    assert(pool.observe(fresh, 0));
    assert(pool.observe(old, 0, true));
    int stable = pool.select(0, allow);
    assert(stable >= 0 && !std::strcmp(pool.entry(stable).endpoint.host, old.host));
    pool.installed(stable);
    TcpRetry retry;
    retry.failed(100);
    pool.sync(retry);
    int next = pool.select(100, allow);
    assert(next >= 0 && next != stable);
    pool.installed(next);
    retry.reset();
    retry.failed(200);
    pool.sync(retry);
    assert(pool.observe(old, 300, true));
    assert(pool.observe(fresh, 300));
    assert(pool.select(300, allow) == -1); // Both cool down; refresh cannot reset it.
    assert(pool.select(10100, allow) == stable);
    retry = pool.entry(stable).retry;
    pool.installed(stable);
    retry.failed(10100);
    pool.sync(retry);
    assert(!pool.entry(stable).retry.ready(20100)); // Second failure still doubles.
    assert(pool.entry(stable).retry.ready(30100));
    assert(pool.select(10200, [](const auto&)
                       { return false; }) == -1);
    next = pool.select(10200, allow);
    assert(next >= 0);
    pool.installed(next);
    retry = pool.entry(next).retry;
    retry.connected(10200);
    pool.sync(retry);
    assert(pool.select(40000, allow) == -1); // Healthy endpoint is retained.

    // Bounded admission protects the active and persisted endpoints, and never
    // evicts a cooling endpoint to bypass its retry history.
    for (char c : {'a', 'b'})
    {
        fresh.host[0] = c;
        assert(pool.observe(fresh, 40000));
    }
    fresh.host[0] = 'c';
    assert(pool.observe(fresh, 40001));
    assert(!std::strcmp(pool.entry(stable).endpoint.host, old.host));
    assert(!std::strcmp(pool.entry(next).endpoint.host, "new.example"));
    pool.reset();
    assert(pool.select(0, allow) == -1);
    for (int i = 0; i < GatewayCandidates::kCapacity; ++i)
    {
        fresh.host[0] = char('a' + i);
        assert(pool.observe(fresh, 0xfffffff0U));
        int index = pool.select(0xfffffff0U, allow);
        assert(index >= 0);
        pool.installed(index);
        retry.reset();
        retry.failed(0xfffffff0U);
        pool.sync(retry);
    }
    fresh.host[0] = 'z';
    assert(!pool.observe(fresh, 1));
    assert(pool.select(1, allow) == -1);
    assert(pool.select(10000, allow) == -1); // Current endpoint is eligible again.
    pool.reset();
    assert(pool.observe(old, 0, true));
    assert(pool.observe(fresh, 0));
    int filtered = pool.select(0, [&](const auto& ep)
                               { return std::strcmp(ep.host, old.host) != 0; });
    assert(filtered >= 0 && !std::strcmp(pool.entry(filtered).endpoint.host, fresh.host));
    // A persisted endpoint is a tie-breaker, not a permanent winner after
    // failures. Prefer an untried peer even when the active cooldown expires.
    pool.reset();
    assert(pool.observe(old, 0, true));
    stable = pool.select(0, allow);
    pool.installed(stable);
    retry.reset();
    retry.failed(0);
    retry.failed(10000);
    pool.sync(retry);
    assert(pool.observe(fresh, 30000));
    next = pool.select(30000, allow);
    assert(next >= 0 && next != stable);
    pool.installed(next);
    retry.reset();
    retry.connected(30000);
    pool.sync(retry);
    assert(pool.select(90000, allow) == -1);
    retry.failed(90000);
    pool.sync(retry);
    // If every alternative is worse, retain the now-eligible active slot.
    assert(pool.select(100000, allow) == -1);
    pool.reset();
    assert(pool.observe(old, 0, true));
    assert(pool.observe(fresh, GatewayCandidates::kDayMs));
    next = pool.select(GatewayCandidates::kDayMs, allow);
    assert(next >= 0 && !std::strcmp(pool.entry(next).endpoint.host, fresh.host));
    // Restoring the same SD record does not pretend a new announce arrived.
    assert(pool.observe(old, GatewayCandidates::kDayMs, true));
    assert(pool.select(GatewayCandidates::kDayMs, allow) == next);
    // Transient entries expire; the last known stable endpoint remains usable.
    stable = pool.select(GatewayCandidates::kExpireMs + GatewayCandidates::kDayMs, allow);
    assert(stable >= 0 && !std::strcmp(pool.entry(stable).endpoint.host, old.host));
    assert(!pool.entry(next).endpoint.port);
    // Expiry must not erase a failed endpoint's outstanding cooldown.
    assert(pool.observe(fresh, 0));
    next = pool.select(0, [&](const auto& ep)
                       { return std::strcmp(ep.host, fresh.host) == 0; });
    assert(next >= 0);
    pool.installed(next);
    retry.reset();
    retry.failed(GatewayCandidates::kExpireMs - 1);
    pool.sync(retry);
    pool.installed(stable);
    pool.select(GatewayCandidates::kExpireMs, allow);
    assert(pool.entry(next).endpoint.port && !pool.entry(next).retry.ready(GatewayCandidates::kExpireMs));
    pool.select(GatewayCandidates::kExpireMs + 10000, allow);
    assert(!pool.entry(next).endpoint.port);
    pool.installed(stable);
    retry.reset();
    retry.connected(GatewayCandidates::kExpireMs);
    pool.sync(retry);
    assert(pool.select(2 * GatewayCandidates::kExpireMs, allow) == -1);
    // Fresh observations win equal-score comparisons across uint32_t wrap.
    pool.reset();
    const uint32_t near_wrap = UINT32_MAX - GatewayCandidates::kDayMs / 2;
    assert(pool.observe(old, near_wrap, true));
    const uint32_t later = near_wrap + GatewayCandidates::kDayMs;
    assert(pool.observe(fresh, later));
    next = pool.select(later, allow);
    assert(next >= 0 && !std::strcmp(pool.entry(next).endpoint.host, fresh.host));
    assert(pool.observe(old, later));
    stable = pool.select(later, allow);
    assert(stable >= 0 && !std::strcmp(pool.entry(stable).endpoint.host, old.host));
}
