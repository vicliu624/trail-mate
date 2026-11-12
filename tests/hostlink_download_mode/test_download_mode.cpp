#include "hostlink/download_mode_owner.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace hm = hostlink::management;
struct Port final : hm::DownloadModePort
{
    bool support = true, connection = true, admissible = true, stopped = false;
    bool claim_ok = true, release_ok = true, queue_ok = true;
    unsigned stops = 0, resumes = 0, claims = 0, releases = 0, reboots = 0;
    uint32_t cookie = 0;
    uint16_t sequence = 0;
    std::array<uint8_t, 32> correlation{};
    std::vector<hm::Status> replies;
    bool supported() const override { return support; }
    bool connected() const override { return connection; }
    bool canBegin() const override { return admissible; }
    void stopStorage() override { ++stops; }
    bool storageStopped() const override { return stopped; }
    bool claimStorage() override
    {
        ++claims;
        return claim_ok;
    }
    bool releaseStorage() override
    {
        ++releases;
        return release_ok;
    }
    void resumeStorage() override { ++resumes; }
    bool reply(const uint8_t* ids, uint16_t seq, hm::Status status, uint32_t token, uint8_t*, size_t) override
    {
        if (!queue_ok) return false;
        std::memcpy(correlation.data(), ids, correlation.size());
        sequence = seq;
        cookie = token;
        replies.push_back(status);
        return true;
    }
    void enterBootloader() override { ++reboots; }
};
struct Fixture
{
    Port port;
    hm::DownloadModeOwner owner{port};
    std::array<uint8_t, 32> ids{};
    uint8_t body[2]{0x91, 0xc3};
    uint8_t scratch[64]{};
    hm::EnvelopeView request{ids.data(), ids.data() + 16, 10, body, sizeof(body)};
    Fixture()
    {
        for (size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<uint8_t>(i + 1);
    }
    void tick(uint32_t now, uint32_t completion = 0) { owner.advance(now, completion, scratch, sizeof(scratch)); }
    void queue(uint32_t now = 10)
    {
        assert(owner.request(request, 101, now) == hm::Status::Ok);
        port.stopped = true;
        tick(now + 1);
        assert(port.replies.back() == hm::Status::Ok && port.cookie != 0 && port.reboots == 0);
    }
};

int main()
{
    // Input, explicit consent, support and admission fail without side effects.
    for (unsigned scenario = 0; scenario < 6; ++scenario)
    {
        Fixture f;
        hm::Status wanted;
        switch (scenario)
        {
        case 0:
            f.request.body_size = 1;
            wanted = hm::Status::InvalidField;
            break;
        case 1:
            f.body[1] = 0xc2;
            wanted = hm::Status::Unauthorized;
            break;
        case 2:
            f.body[1] = 1;
            wanted = hm::Status::InvalidField;
            break;
        case 3:
            f.port.support = false;
            wanted = hm::Status::Unsupported;
            break;
        case 4:
            f.port.connection = false;
            wanted = hm::Status::Busy;
            break;
        default:
            f.port.admissible = false;
            wanted = hm::Status::Busy;
            break;
        }
        assert(f.owner.request(f.request, 101, 1) == wanted);
        assert(!f.owner.active() && f.port.stops == 0 && f.port.reboots == 0);
    }
    {
        Fixture f;
        const auto original = f.ids;
        assert(f.owner.request(f.request, 101, 10) == hm::Status::Ok);
        f.ids.fill(0); // Borrowed command storage can now be reused.
        f.tick(11);
        assert(f.port.replies.empty() && f.port.stops == 1);
        assert(f.owner.request(f.request, 101, 12) == hm::Status::Busy);
        f.port.stopped = true;
        f.tick(20);
        assert(f.port.correlation == original && f.port.sequence == 101);
        const auto cookie = f.port.cookie;
        f.tick(21, cookie + 1);
        assert(!f.port.reboots);
        f.tick(22, cookie);
        f.port.connection = false; // A host closes after receiving the response.
        f.tick(121);
        assert(!f.port.reboots);
        f.tick(122);
        assert(f.port.reboots == 1);
        f.tick(123); // Simulate a platform restart failing and returning.
        assert(!f.owner.active() && f.port.resumes == 1 && f.port.releases == 1);
    }
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        Fixture f;
        f.queue();
        const auto cookie = f.port.cookie;
        if (scenario == 0) f.port.connection = false;
        f.tick(scenario == 1 ? 2011 : 12, scenario == 2 ? cookie | hm::DownloadModeOwner::kDiscardedBit : 0);
        assert(!f.owner.active() && f.port.reboots == 0 && f.port.resumes == 1);
        f.port.connection = true;
        f.queue(3000);
        f.tick(3002, cookie); // A stale write from the first request is ignored.
        assert(f.port.reboots == 0 && f.owner.active());
    }
    {
        Fixture f;
        assert(f.owner.request(f.request, 101, 1) == hm::Status::Ok);
        f.tick(2001); // A stuck owner must never trigger a forced reboot.
        assert(f.owner.active() && !f.port.reboots && !f.port.resumes);
        f.port.stopped = true;
        f.tick(2002);
        assert(!f.owner.active() && f.port.resumes == 1);
    }
    for (unsigned scenario = 0; scenario < 2; ++scenario)
    {
        Fixture f;
        f.port.claim_ok = scenario == 0 ? false : true;
        f.port.queue_ok = false;
        assert(f.owner.request(f.request, 101, 1) == hm::Status::Ok);
        f.port.stopped = true;
        f.tick(2);
        f.tick(2001);
        assert(!f.owner.active() && !f.port.reboots && f.port.resumes == 1);
    }
    {
        Fixture f;
        f.queue(0xfffffff0U);
        f.tick(0xfffffff2U, f.port.cookie);
        f.tick(0x56U);
        assert(f.port.reboots == 1); // Grace deadline crosses uint32 wrap.
    }
    std::puts("download mode: validation, correlation, delayed reply, drop, timeout, restoration and wrap PASS");
}
