#include "platform/shared/radio/fm_receiver.h"
#include <cassert>
#include <vector>

using namespace platform::shared::radio;
struct Port : FmReceiver::Port
{
    std::vector<int> calls;
    bool radio = true, audio = true, receive = true, cleanup = true, short_write = false;
    bool acquireRadio() override
    {
        calls.push_back(1);
        return radio;
    }
    bool acquireAudio(uint32_t rate) override
    {
        assert(rate == 48000);
        calls.push_back(2);
        return audio;
    }
    bool startIq(uint32_t hz, uint32_t rate) override
    {
        assert(hz >= 87500000 && hz <= 108000000 && rate == 250000);
        calls.push_back(3);
        return receive;
    }
    bool stopIq() override
    {
        calls.push_back(4);
        return cleanup;
    }
    size_t writeAudio(const int16_t* pcm, size_t count) override
    {
        assert(pcm && count);
        calls.push_back(5);
        return short_write ? count - 1 : count;
    }
    bool releaseAudio() override
    {
        calls.push_back(6);
        return cleanup;
    }
    bool releaseRadio() override
    {
        calls.push_back(7);
        return cleanup;
    }
};
int main()
{
    static Port port;
    static FmReceiver fm(port);
    uint8_t iq[1280]{};
    for (size_t n = 0; n < sizeof(iq); n += 2) iq[n] = 31;
    assert(!fm.start(87499999));
    assert(port.calls.empty());
    assert(fm.start(100000000));
    assert(fm.snapshot().phase == FmReceiver::Phase::Receiving);
    auto session = fm.snapshot().session;
    assert(!fm.consume(session - 1, iq, sizeof(iq), 250000, 1, 0, FmIqConsumer::Format::Iq8));
    assert(fm.snapshot().pcm_samples == 0);
    assert(fm.consume(session, iq, sizeof(iq), 250000, 1, 0, FmIqConsumer::Format::Iq8));
    assert(fm.snapshot().phase == FmReceiver::Phase::Playing);
    assert(fm.snapshot().pcm_samples > 0);
    assert(!fm.start(108000001));
    assert(fm.snapshot().phase == FmReceiver::Phase::Playing);
    assert(!fm.consume(session, iq, sizeof(iq), 250000, 1, 641, FmIqConsumer::Format::Iq8));
    assert(fm.snapshot().error == FmReceiver::Error::Discontinuity);
    assert((port.calls == std::vector<int>{1, 2, 3, 5, 4, 6, 7}));
    port.calls.clear();
    assert(fm.stop());
    assert(port.calls.empty());
    assert(fm.start(87500000));
    assert(fm.snapshot().session != session);
    port.calls.clear();
    assert(fm.start(108000000));
    assert((port.calls == std::vector<int>{4, 6, 7, 1, 2, 3}));
    session = fm.snapshot().session;
    port.short_write = true;
    assert(!fm.consume(session, iq, sizeof(iq), 250000, 1, 0, FmIqConsumer::Format::Iq8));
    assert(fm.snapshot().error == FmReceiver::Error::AudioWrite);
    assert(fm.snapshot().pcm_samples == 0);
    port.short_write = false;
    port.calls.clear();
    port.radio = false;
    assert(!fm.start(100000000));
    assert(fm.snapshot().error == FmReceiver::Error::RadioBusy);
    assert((port.calls == std::vector<int>{1}));
    port.calls.clear();
    port.radio = true;
    port.audio = false;
    assert(!fm.start(100000000));
    assert((port.calls == std::vector<int>{1, 2, 7}));
    port.calls.clear();
    port.audio = true;
    port.receive = false;
    port.cleanup = false;
    assert(!fm.start(100000000));
    assert((port.calls == std::vector<int>{1, 2, 3, 4, 6, 7}));
    assert(fm.snapshot().error == FmReceiver::Error::ReceiveStart);
    assert(fm.snapshot().cleanup_failed);
    port.cleanup = true;
    port.receive = true;
    assert(fm.start(100000000));
    session = fm.snapshot().session;
    assert(!fm.consume(session, iq, sizeof(iq), 125000, 1, 0, FmIqConsumer::Format::Iq8));
    assert(fm.snapshot().error == FmReceiver::Error::InvalidIq);
    assert(fm.stop());
}
