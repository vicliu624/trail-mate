#include "platform/ui/team_ui_store_runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

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
        if (entry.path().filename() == "posring.log") ring = entry.path();
    assert(!ring.empty() && std::filesystem::file_size(ring) == 24 + 28);
    std::filesystem::resize_file(ring, 24 + 24);
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.size() == 1 && has(0x4BE5DDEF, 1700000000));
    assert(append(0xF48DA2AE, 1700000030));
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.size() == 2 && has(0xF48DA2AE, 1700000030));

    // V1 files produced by the old writer lack the final four reserved bytes.
    // Read the valid fields and then append without seeking beyond that tail.
    std::filesystem::resize_file(ring, std::filesystem::file_size(ring) - 4);
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.size() == 2 && has(0xF48DA2AE, 1700000030));
    {
        // The old writer could advance the header even if a sparse seek failed.
        std::fstream legacy(ring, std::ios::in | std::ios::out | std::ios::binary);
        legacy.seekp(12);
        const char cursor[4] = {84, 0, 0, 0};
        legacy.write(cursor, sizeof(cursor));
    }
    assert(append(0xA1B3B57C, 1700000060));
    assert(std::filesystem::file_size(ring) == 24 + 3 * 28);
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(samples.size() == 3 && has(0xA1B3B57C, 1700000060));
    for (uint32_t index = 1; index <= 520; ++index) assert(append(0x4BE5DDEF, 1700000090 + 30 * index));
    assert(std::filesystem::file_size(ring) == 24 + 512 * 28);
    assert(team::ui::team_ui_posring_load_latest(id, samples));
    assert(has(0x4BE5DDEF, 1700000090 + 30 * 520));
    std::filesystem::remove_all(root);
}
