#include "display/drivers/ST7796.h"

namespace display
{
namespace drivers
{

// ST7796 initialization command sequence
static const CommandTable_t st7796_init_commands[] = {
    {0x01, {0x00}, 0x80},                                                                               // Software reset, delay 120ms
    {0x11, {0x00}, 0x80},                                                                               // Sleep out, delay 120ms
    {0xF0, {0xC3}, 0x01},                                                                               // Command Set Control 1
    {0xF0, {0xC3}, 0x01},                                                                               // Command Set Control 1
    {0xF0, {0x96}, 0x01},                                                                               // Command Set Control 1
    {0x36, {0x48}, 0x01},                                                                               // Memory Access Control
    {0x3A, {0x55}, 0x01},                                                                               // Pixel Format Set (16-bit/pixel)
    {0xB4, {0x01}, 0x01},                                                                               // Display Inversion Control
    {0xB6, {0x80, 0x02, 0x3B}, 0x03},                                                                   // Display Function Control
    {0xE8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 0x08},                                     // Power Control 1
    {0xC1, {0x06}, 0x01},                                                                               // Power Control 2
    {0xC2, {0xA7}, 0x01},                                                                               // Power Control 3
    {0xC5, {0x18}, 0x81},                                                                               // VCOM Control, delay 120ms
    {0xE0, {0xF0, 0x09, 0x0b, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 0x0F}, // Positive Voltage Gamma Control
    {0xE1, {0xE0, 0x09, 0x0b, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 0x8F}, // Negative Voltage Gamma Control
    {0xF0, {0x3c}, 0x01},                                                                               // Command Set Control 1
    {0xF0, {0x69}, 0x81},                                                                               // Command Set Control 1, delay 120ms
    {0x21, {0x00}, 0x01},                                                                               // Display Inversion On
    {0x29, {0x00}, 0x01},                                                                               // Display On
};

const CommandTable_t* ST7796::getInitCommands()
{
    return st7796_init_commands;
}

size_t ST7796::getInitCommandsCount()
{
    return sizeof(st7796_init_commands) / sizeof(st7796_init_commands[0]);
}

const DispRotationConfig_t* ST7796::getRotationConfig(uint16_t width, uint16_t height,
                                                      uint16_t landscape_offset_x,
                                                      uint16_t portrait_offset_y)
{
    // Rotation configurations for ST7796
    // Format: {MADCTL, width, height, offset_x, offset_y}
    // MADCTL values control display orientation and color order
    //
    // Portrait orientations (0°, 180°): use portrait_offset_y for vertical offset
    // Landscape orientations (90°, 270°): use landscape_offset_x for horizontal offset
    static DispRotationConfig_t rotation_configs[4];

    // Portrait 0° (rotated): width=height, height=width, offset_x=0, offset_y=portrait_offset_y
    rotation_configs[0] = {0xE8, height, width, 0, portrait_offset_y};

    // Landscape 90° (normal): width=width, height=height, offset_x=landscape_offset_x, offset_y=0
    rotation_configs[1] = {0x48, width, height, landscape_offset_x, 0};

    // Portrait 180° (rotated): width=height, height=width, offset_x=0, offset_y=portrait_offset_y
    rotation_configs[2] = {0x28, height, width, 0, portrait_offset_y};

    // Landscape 270° (upside down): width=width, height=height, offset_x=landscape_offset_x, offset_y=0
    rotation_configs[3] = {0x88, width, height, landscape_offset_x, 0};

    return rotation_configs;
}

} // namespace drivers
} // namespace display
