#include "platform/esp/arduino_common/chat/infra/reticulum/discovery_stamp_verifier.h"

#include <cassert>
#include <cstring>
#include <memory>

const void* discovery_test_psram = nullptr;
using Verifier = chat::reticulum::DiscoveryStampVerifier;
static_assert(sizeof(Verifier) <= 512, "Discovery verification must stay small");

int main()
{
    // Generated independently with discovery_stamp_reference.py. Expected final
    // digest: 0000761540798b181766bff7cf26387c42fe223f77d5ab64986763ad3134f839.
    const uint8_t payload[] = "Reticulum discovery conformance";
    uint8_t stamp[32] = {};
    stamp[30] = 0xd2;
    stamp[31] = 0xe0;
    auto verifier = std::make_unique<Verifier>();
    assert(!verifier->begin(payload, sizeof(payload) - 1, stamp));
    assert(verifier->state() == Verifier::State::Invalid); // Internal RAM denied.
    discovery_test_psram = verifier.get();
    assert(verifier->begin(payload, sizeof(payload) - 1, stamp));
    assert(!verifier->begin(payload, sizeof(payload) - 1, stamp)); // Pending job retained.
    assert(verifier->rounds() == 0);
    for (unsigned round = 1; round < 20; ++round)
    {
        assert(verifier->poll() == Verifier::State::Pending);
        assert(verifier->rounds() == round);
    }
    assert(verifier->poll() == Verifier::State::Valid);
    assert(verifier->poll() == Verifier::State::Valid && verifier->rounds() == 20);
    stamp[31] ^= 1;
    assert(verifier->begin(payload, sizeof(payload) - 1, stamp));
    for (unsigned i = 0; i < 20; ++i) verifier->poll();
    assert(verifier->state() == Verifier::State::Invalid);
    stamp[31] ^= 1;
    uint8_t altered[sizeof(payload)];
    std::memcpy(altered, payload, sizeof(payload));
    altered[0] ^= 1;
    assert(verifier->begin(altered, sizeof(altered) - 1, stamp));
    for (unsigned i = 0; i < 20; ++i) verifier->poll();
    assert(verifier->state() == Verifier::State::Invalid);
    assert(verifier->begin(payload, sizeof(payload) - 1, stamp));
    verifier->poll();
    verifier->reset();
    assert(verifier->state() == Verifier::State::Idle && verifier->rounds() == 0);
    assert(verifier->poll() == Verifier::State::Idle);
    assert(!verifier->begin(nullptr, 1, stamp));
    assert(!verifier->begin(payload, 0, stamp));
    assert(!verifier->begin(payload, 468, stamp));
    assert(!verifier->begin(payload, sizeof(payload) - 1, nullptr));
    return 0;
}
