#include "platform/esp/arduino_common/geocaching/sd_journal_reclaim.h"
#include "runtime_environment.h"
#include <cstdio>

namespace sd = platform::esp::arduino_common::geocaching;
namespace gc = geocaching::storage;
namespace test = runtime_test;
static void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
static std::string path(unsigned sequence)
{
    char name[80];
    std::snprintf(name, sizeof(name), "/trailmate/geocaching/.state/journal/%016llx.gcj", static_cast<unsigned long long>(sequence));
    return name;
}
static void segment(unsigned first, unsigned last)
{
    auto& bytes = test::files[path(first)];
    for (unsigned sequence = first; sequence <= last; ++sequence)
    {
        std::vector<uint8_t> payload{0x93, 1, static_cast<uint8_t>(sequence - 1), 0x91, 0x93, 11, 0xc4, 32};
        payload.insert(payload.end(), 32, 0x71);
        payload.insert(payload.end(), {0xc4, 3, 0x92, 0x90, 0x90});
        gc::RecordHeader header;
        require(gc::makeRecordHeader(gc::RecordKind::Transaction, sequence, {payload.data(), payload.size()}, header), "encode frame");
        bytes.insert(bytes.end(), header.begin(), header.end());
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
}
static sd::JournalReclaimStep pump(sd::SdJournalReclaim& job)
{
    auto state = sd::JournalReclaimStep::Working;
    for (unsigned i = 0; i < 10000 && state == sd::JournalReclaimStep::Working; ++i)
    {
        test::io_bytes = 0;
        state = job.step();
        require(test::io_bytes <= 512, "reclamation exceeded transfer budget");
    }
    return state;
}
int main()
{
    gc::VolumeInstance volume{}, confirmed;
    require(sd::createNewSdVolume(volume, confirmed) == sd::SdVolumeResult::Ready, "create volume");
    test::directories.insert("/trailmate/geocaching/.state/journal");
    segment(1, 2);
    segment(3, 6);
    segment(7, 8);
    const auto original = test::files;
    std::array<uint8_t, 8192> frame{};
    {
        sd::SdJournalReclaim job(volume);
        require(job.begin(4, 8, frame.data(), frame.size()), "begin straddling segment reclamation");
        require(pump(job) == sd::JournalReclaimStep::Complete, "complete reclamation");
        require(job.removedSegments() == 1 && !test::files.count(path(1)), "covered prefix not reclaimed");
        require(test::files.at(path(3)) == original.at(path(3)) && test::files.at(path(7)) == original.at(path(7)), "retained suffix changed");
    }
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        test::files = original;
        if (fault == 0) test::files.erase(path(7));
        if (fault == 1) test::files[path(7)].back() ^= 1;
        if (fault == 2) test::files[path(7)].pop_back();
        const auto damaged = test::files;
        sd::SdJournalReclaim job(volume);
        require(job.begin(4, 8, frame.data(), frame.size()), "begin damaged suffix");
        require(pump(job) == sd::JournalReclaimStep::Invalid, "damaged suffix accepted");
        require(test::files == damaged, "deleted files before proving retained suffix");
    }
    // Interrupt at every step before the first deletion. A same-volume external
    // write invalidates prior proof, even when the reclaimer was ready to remove.
    for (unsigned cut = 0; cut < 160; ++cut)
    {
        test::files = original;
        sd::SdJournalReclaim job(volume);
        require(job.begin(4, 8, frame.data(), frame.size()), "begin interrupted reclamation");
        for (unsigned i = 0; i < cut; ++i)
        {
            require(job.step() == sd::JournalReclaimStep::Working, "unexpected early completion");
            if (job.removedSegments()) break;
        }
        if (job.removedSegments()) break;
        test::external_owner = true;
        require(job.step() == sd::JournalReclaimStep::Busy, "external owner ignored");
        test::files[path(7)].back() ^= 1;
        const auto changed = test::files;
        test::external_owner = false;
        require(pump(job) == sd::JournalReclaimStep::Invalid, "stale suffix proof reused");
        require(test::files == changed, "removed prefix after external suffix corruption");
    }
    require(!test::open_files && !test::open_dirs && !test::blocked_io, "reclamation leaked handles or accessed blocked SD");
    return 0;
}
