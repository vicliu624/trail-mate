#include "platform/ui/team_ui_store_runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <vector>

namespace team_store_test
{
extern bool fail_seek;
extern bool fail_flush;
extern bool fail_record_read;
extern bool short_record_write;
extern bool fail_update_open;
extern bool fail_header_read;
extern std::size_t largest_read;
} // namespace team_store_test

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-team-position-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_TEST_SD_ROOT", root.c_str(), 1);
    const team::TeamId id{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<team::ui::TeamPosSample> samples;
    const auto append = [&](uint32_t member, uint32_t time)
    { return team::ui::team_ui_posring_append(id, member, 250389000, 1027183000, 123, 45, time); };
    const auto has = [&](uint32_t member, uint32_t time)
    {
        return std::any_of(samples.begin(), samples.end(), [&](const auto& sample)
                           { return sample.member_id == member && sample.ts == time && sample.lat_e7 == 250389000 && sample.lon_e7 == 1027183000 && sample.alt_m == 123 && sample.speed_dmps == 45; });
    };
    assert(append(0x4BE5DDEF, 1700000000));
    const bool first_visible = team::ui::team_ui_posring_load_latest(id, samples);
    std::printf("First production Arduino position readable immediately: %d\n", first_visible);
    std::fflush(stdout);
    assert(first_visible && samples.size() == 1 && has(0x4BE5DDEF, 1700000000));
    std::filesystem::path ring;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.path().filename() == "posring-v2.log") ring = entry.path();
    assert(!ring.empty() && std::filesystem::file_size(ring) == 24 + 28);
    const auto contents = [&]()
    {
        std::ifstream file(ring, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const auto first_record = contents();
    std::filesystem::resize_file(ring, 24 + 24);
    assert(!team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.empty());
    assert(!append(0x12340010, 1700000030));
    assert(std::filesystem::file_size(ring) == 24 + 24); // No tail padding or cursor repair.
    {
        std::ofstream file(ring, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(first_record.data()), first_record.size());
    }
    assert(append(0xF48DA2AE, 1700000030));
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.size() == 2 && has(0xF48DA2AE, 1700000030));

    assert(append(0xA1B3B57C, 1700000060));
    assert(std::filesystem::file_size(ring) == 24 + 3 * 28);
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.size() == 3 && has(0xA1B3B57C, 1700000060));
    for (uint32_t index = 1; index <= 520; ++index) assert(append(0x4BE5DDEF, 1700000090 + 30 * index));
    assert(std::filesystem::file_size(ring) == 24 + 512 * 28);
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(has(0x4BE5DDEF, 1700000090 + 30 * 520));
    assert(team_store_test::largest_read <= 28);

    const auto full_ring = contents();
    team_store_test::fail_update_open = true;
    assert(!append(0x12340005, 1700099910));
    assert(contents() == full_ring);
    team_store_test::fail_update_open = false;
    team_store_test::fail_header_read = true;
    assert(!append(0x12340006, 1700099940));
    assert(contents() == full_ring);
    team_store_test::fail_header_read = false;
    team_store_test::fail_seek = true;
    assert(!append(0x12340001, 1700100000));
    assert(contents() == full_ring);
    team_store_test::fail_seek = false;
    team_store_test::fail_record_read = true;
    assert(!team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.empty() && contents() == full_ring);
    team_store_test::fail_record_read = false;

    // An incomplete record must not commit an advanced header cursor.
    team_store_test::short_record_write = true;
    assert(!append(0x12340002, 1700100030));
    const auto short_write = contents();
    assert(std::equal(full_ring.begin(), full_ring.begin() + 24, short_write.begin()));
    team_store_test::short_record_write = false;
    assert(append(0x12340002, 1700100060));
    team_store_test::fail_flush = true;
    assert(!append(0x12340003, 1700100090));
    team_store_test::fail_flush = false;

    // Unsupported/corrupt historical headers are never rewritten on append.
    auto corrupt = contents();
    corrupt[0] = 0;
    {
        std::ofstream file(ring, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(corrupt.data()), corrupt.size());
    }
    assert(!append(0x12340004, 1700100120));
    assert(contents() == corrupt);
    assert(!team::ui::team_ui_posring_load_latest(id, samples));
    assert(contents() == corrupt);
    std::puts("V2 records, wrap, bounded reads and injected I/O failures passed.");
    std::filesystem::remove_all(root);
}
