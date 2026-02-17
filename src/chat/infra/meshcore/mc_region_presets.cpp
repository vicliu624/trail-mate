/**
 * @file mc_region_presets.cpp
 * @brief MeshCore region preset table
 */

#include "mc_region_presets.h"

namespace chat
{
namespace meshcore
{

namespace
{

// Source of truth:
// https://api.meshcore.nz/api/v1/config -> suggested_radio_settings.entries
// Snapshot aligned on 2026-02-17.
const RegionPreset kRegionPresets[] = {
    {1, "Australia", 915.800f, 250.0f, 10, 5},
    {2, "Australia (Narrow)", 916.575f, 62.5f, 7, 8},
    {3, "Australia: SA, WA", 923.125f, 62.5f, 8, 8},
    {4, "Australia: QLD", 923.125f, 62.5f, 8, 5},
    {5, "EU/UK (Narrow)", 869.618f, 62.5f, 8, 8},
    {6, "EU/UK (Long Range)", 869.525f, 250.0f, 11, 5},
    {7, "EU/UK (Medium Range)", 869.525f, 250.0f, 10, 5},
    {8, "Czech Republic (Narrow)", 869.432f, 62.5f, 7, 5},
    {9, "EU 433MHz (Long Range)", 433.650f, 250.0f, 11, 5},
    {10, "New Zealand", 917.375f, 250.0f, 11, 5},
    {11, "New Zealand (Narrow)", 917.375f, 62.5f, 7, 5},
    {12, "Portugal 433", 433.375f, 62.5f, 9, 6},
    {13, "Portugal 868", 869.618f, 62.5f, 7, 6},
    {14, "Switzerland", 869.618f, 62.5f, 8, 8},
    {15, "USA/Canada (Recommended)", 910.525f, 62.5f, 7, 5},
    {16, "Vietnam", 920.250f, 250.0f, 11, 5},
};

} // namespace

const RegionPreset* getRegionPresetTable(size_t* out_count)
{
    if (out_count)
    {
        *out_count = sizeof(kRegionPresets) / sizeof(kRegionPresets[0]);
    }
    return kRegionPresets;
}

const RegionPreset* findRegionPresetById(uint8_t id)
{
    for (const auto& preset : kRegionPresets)
    {
        if (preset.id == id)
        {
            return &preset;
        }
    }
    return nullptr;
}

bool isValidRegionPresetId(uint8_t id)
{
    return id == 0 || findRegionPresetById(id) != nullptr;
}

} // namespace meshcore
} // namespace chat

