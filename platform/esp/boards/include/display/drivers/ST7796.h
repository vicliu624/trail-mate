#pragma once

#include "../DisplayInterface.h"

namespace display
{
namespace drivers
{

/**
 * @brief ST7796 display driver configuration
 *
 * This driver provides initialization commands and rotation configurations
 * for the ST7796 display controller. It can be used by any board that
 * uses the ST7796 display chip.
 */
class ST7796
{
  public:
    /**
     * @brief Get the initialization command table for ST7796
     * @return Pointer to the command table array
     */
    static const CommandTable_t* getInitCommands();

    /**
     * @brief Get the number of initialization commands
     * @return Number of commands in the init table
     */
    static size_t getInitCommandsCount();

    /**
     * @brief Get the rotation configuration for ST7796
     * @param width Display width in pixels
     * @param height Display height in pixels
     * @param landscape_offset_x X offset for landscape orientations (90°, 270°), default: 0
     * @param portrait_offset_y Y offset for portrait orientations (0°, 180°), default: 0
     * @return Pointer to the rotation configuration array
     *
     * @note The offset values are board-specific and depend on the physical
     *       display mounting. For T-LoRa-Pager:
     *       - Landscape orientations (90°, 270°): use landscape_offset_x=49
     *       - Portrait orientations (0°, 180°): use portrait_offset_y=49
     */
    static const DispRotationConfig_t* getRotationConfig(uint16_t width, uint16_t height,
                                                         uint16_t landscape_offset_x = 0,
                                                         uint16_t portrait_offset_y = 0);

    /**
     * @brief Get the number of rotation configurations
     * @return Number of rotation configurations (always 4)
     */
    static constexpr size_t getRotationConfigCount() { return 4; }
};

} // namespace drivers
} // namespace display
