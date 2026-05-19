/**
 * @file mc_region_presets.cpp
 * @brief MeshCore region preset table
 */

#include "chat/infra/meshcore/mc_region_presets.h"

namespace chat
{
namespace meshcore
{

namespace
{

const RegionPreset kRegionPresets[] = {
    {1, "Australia", 915.800f, 250.0f, 10, 5, 20},
    {2, "Australia (Narrow)", 916.575f, 62.5f, 7, 8, 20},
    {3, "Australia: SA, WA", 923.125f, 62.5f, 8, 8, 20},
    {4, "Australia: QLD", 923.125f, 62.5f, 8, 5, 20},
    {5, "EU/UK (Narrow)", 869.618f, 62.5f, 8, 8, 20},
    {6, "EU/UK (Long Range)", 869.525f, 250.0f, 11, 5, 20},
    {7, "EU/UK (Medium Range)", 869.525f, 250.0f, 10, 5, 20},
    {8, "Czech Republic (Narrow)", 869.432f, 62.5f, 7, 5, 20},
    {9, "EU 433MHz (Long Range)", 433.650f, 250.0f, 11, 5, 20},
    {10, "New Zealand", 917.375f, 250.0f, 11, 5, 20},
    {11, "New Zealand (Narrow)", 917.375f, 62.5f, 7, 5, 20},
    {12, "Portugal 433", 433.375f, 62.5f, 9, 6, 20},
    {13, "Portugal 868", 869.618f, 62.5f, 7, 6, 20},
    {14, "Switzerland", 869.618f, 62.5f, 8, 8, 20},
    {15, "USA/Canada (Recommended)", 910.525f, 62.5f, 7, 5, 20},
    {16, "Vietnam", 920.250f, 250.0f, 11, 5, 20},
    {17, "CN", 495.200f, 125.0f, 9, 5, 12},
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
