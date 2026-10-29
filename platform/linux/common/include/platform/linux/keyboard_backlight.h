#pragma once
#include "platform/linux/backlight.h"
#include <string>

namespace platform::linux_runtime
{
struct KeyboardBacklightOptions
{
    std::filesystem::path root = "/sys/class/pwm";
    std::filesystem::path device_tree_root = "/sys/firmware/devicetree/base";
    std::filesystem::path of_node;
    uint16_t channel = 0;
    uint32_t initial_period_ns = 0;
    uint16_t max_duty_permille = 0;
    bool export_channel = false;
};
inline bool valid_keyboard_backlight_options(const KeyboardBacklightOptions& options)
{
    if (!options.root.is_absolute() || !options.device_tree_root.is_absolute()) return false;
    if (options.of_node.empty()) return !options.initial_period_ns && !options.max_duty_permille && !options.export_channel && !options.channel;
    if (!options.of_node.is_absolute() || options.of_node == "/" || options.of_node.string().size() > 512 || options.channel > 255 ||
        !options.initial_period_ns || options.initial_period_ns > 1000000000U ||
        !options.max_duty_permille || options.max_duty_permille > 1000) return false;
    for (const auto& component : options.of_node)
        if (component == ".." || component == ".") return false;
    return true;
}
class KeyboardBacklightRuntime
{
  public:
    explicit KeyboardBacklightRuntime(KeyboardBacklightOptions);
    ~KeyboardBacklightRuntime();
    KeyboardBacklightRuntime(const KeyboardBacklightRuntime&) = delete;
    KeyboardBacklightRuntime& operator=(const KeyboardBacklightRuntime&) = delete;
    BacklightSnapshot snapshot() const;
    bool request(uint8_t level);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool configure_keyboard_backlight(const KeyboardBacklightOptions&);
void prepare_keyboard_backlight();
BacklightSnapshot keyboard_backlight_snapshot();
bool request_keyboard_backlight(uint8_t level);
void shutdown_keyboard_backlight();
} // namespace platform::linux_runtime
