/**
 * @file mc_region_presets.h
 * @brief MeshCore region preset table
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace meshcore
{

struct RegionPreset
{
    uint8_t id;
    const char* title;
    float freq_mhz;
    float bw_khz;
    uint8_t sf;
    uint8_t cr;
};

const RegionPreset* getRegionPresetTable(size_t* out_count);
const RegionPreset* findRegionPresetById(uint8_t id);
bool isValidRegionPresetId(uint8_t id);

} // namespace meshcore
} // namespace chat

