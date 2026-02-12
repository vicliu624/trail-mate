#include "display/drivers/ST7789WatchS3.h"

namespace display
{
namespace drivers
{

// Command sequence derived from LilyGo ST7789 init (240x240 panel).
// The high bit (0x80) in len requests a post-command delay in DisplayInterface.
static const CommandTable_t kInit[] = {
    {0x11, {0}, 0x80},                                                                                // SLPOUT + delay
    {0x13, {0}, 0},                                                                                   // NORON
    {0x36, {0x00}, 1},                                                                                // MADCTL (RGB order)
    {0x3A, {0x55}, 1},                                                                                // COLMOD (RGB565)
    {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5},                                                        // PORCTRL
    {0xB7, {0x75}, 1},                                                                                // GCTRL
    {0xBB, {0x1A}, 1},                                                                                // VCOMS
    {0xC0, {0x2C}, 1},                                                                                // LCMCTRL
    {0xC2, {0x01}, 1},                                                                                // VDVVRHEN
    {0xC3, {0x13}, 1},                                                                                // VRHS
    {0xC4, {0x20}, 1},                                                                                // VDVSET
    {0xC6, {0x0F}, 1},                                                                                // FRCTR2
    {0xD0, {0xA4, 0xA1}, 2},                                                                          // PWCTRL1
    {0xE0, {0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30}, 14}, // PVGAMCTRL
    {0xE1, {0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x3E}, 14}, // NVGAMCTRL
    {0x21, {0}, 0},                                                                                   // INVON
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},                                                              // CASET 0..239
    {0x2B, {0x00, 0x00, 0x00, 0xEF}, 4},                                                              // RASET 0..239
    {0x29, {0}, 0x80},                                                                                // DISPON + delay
};

const CommandTable_t* ST7789WatchS3::getInitCommands()
{
    return kInit;
}

size_t ST7789WatchS3::getInitCommandsCount()
{
    return sizeof(kInit) / sizeof(kInit[0]);
}

const DispRotationConfig_t* ST7789WatchS3::getRotationConfig(uint16_t width, uint16_t height)
{
    // Generic ST7789 rotation table; use RGB order to match LVGL swap path.
    static DispRotationConfig_t rot[4];
    constexpr uint16_t kOffset = 80;
    rot[0] = {static_cast<uint8_t>(0x00), width, height, 0, kOffset};
    rot[1] = {static_cast<uint8_t>(0x60), height, width, kOffset, 0};
    rot[2] = {static_cast<uint8_t>(0xC0), width, height, 0, kOffset};
    rot[3] = {static_cast<uint8_t>(0xA0), height, width, kOffset, 0};
    return rot;
}

} // namespace drivers
} // namespace display
