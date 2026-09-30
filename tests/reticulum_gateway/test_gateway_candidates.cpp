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
}
