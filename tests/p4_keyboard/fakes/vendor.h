#pragma once
#include "lvgl.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#define DEVICE_MAX_BRIGHTNESS_LEVEL 255
constexpr int I2C_NUM_0 = 0;
namespace fake
{
inline bool attached = true;
inline bool transfer_ok = true;
inline bool probe_timeout = false;
inline std::vector<uint16_t> probe_addresses;
inline int probes = 0;
inline int shortcuts = 0;
inline int exits = 0;
inline bool app_active = false;
inline std::vector<int> reset_steps;
} // namespace fake
namespace cpp_bus_driver
{
class HardwareI2c1
{
  public:
    HardwareI2c1(int sda, int scl, int port)
    {
        assert(sda == 46 && scl == 45 && port == I2C_NUM_0);
    }
    explicit HardwareI2c1(const std::shared_ptr<HardwareI2c1>& owner) : owner_(owner) {}
    bool InitBus() { return true; }
    HardwareI2c1* bus_handle() { return this; }
    bool Probe(unsigned address)
    {
        if (address == 0x20) ++fake::probes;
        return fake::attached && (address == 0x20 || address == 0x34);
    }
    bool Deinit(bool delete_bus)
    {
        assert(delete_bus == !owner_);
        return true;
    }

  private:
    std::shared_ptr<HardwareI2c1> owner_;
};
class Xl95x5
{
  public:
    enum class Pin
    {
        kIo6
    };
    enum class Mode
    {
        kOutput
    };
    Xl95x5(std::shared_ptr<HardwareI2c1>, uint16_t) {}
    bool Init()
    {
        return fake::attached;
    }
    bool SetGpioMode(Pin, Mode)
    {
        fake::reset_steps.push_back(2);
        return true;
    }
    bool GpioWrite(Pin, int value)
    {
        fake::reset_steps.push_back(value);
        return true;
    }
};
class Tca8418
{
  public:
    enum class EventType
    {
        kKeypad,
        kGpio
    };
    enum class IrqMask
    {
        kKeyEvents
    };
    enum class IrqFlag
    {
        kKeyEvents
    };
    struct TouchInfo
    {
        uint8_t num;
        bool press_flag;
        EventType event_type = EventType::kKeypad;
    };
    struct IrqStatus
    {
        bool fifo_overflow_flag = false;
    };
    inline static uint8_t irq_flags = 0;
    uint8_t GetIrqFlag() { return fake::transfer_ok ? irq_flags : 0xff; }
    bool ParseIrqStatus(uint8_t flags, IrqStatus& status)
    {
        status.fifo_overflow_flag = (flags & 8) != 0;
        return flags != 0xff;
    }
    bool ClearIrqFlag(uint8_t)
    {
        irq_flags = 0;
        return fake::transfer_ok;
    }
    bool ClearEventFifo()
    {
        fifo.clear();
        return fake::transfer_ok;
    }
    inline static std::vector<TouchInfo> fifo;
    Tca8418(std::shared_ptr<HardwareI2c1>, uint16_t) {}
    bool Init() { return fake::attached; }
    bool SetKeypadScanWindow(int x, int y, int w, int h) { return x == 0 && y == 0 && w == 10 && h == 7; }
    bool SetInterruptEnable(IrqMask) { return true; }
    bool ClearIrqFlag(IrqFlag) { return fake::transfer_ok; }
    uint8_t GetFingerCount() { return fake::transfer_ok ? static_cast<uint8_t>(fifo.size()) : 0xff; }
    bool ReadKeyEvent(TouchInfo* out)
    {
        if (!fake::transfer_ok || fifo.empty()) return false;
        *out = fifo.front();
        fifo.erase(fifo.begin());
        return true;
    }
};
} // namespace cpp_bus_driver
namespace boards::t_display_p4
{
struct BoardProfile
{
    struct KeyboardModule
    {
        int sda = 46, scl = 45, interrupt = 48, backlight = 47;
        uint16_t xl9555 = 0x20, tca8418 = 0x34;
        int columns = 10, rows = 7;
    };
    KeyboardModule keyboard;
    bool supports_keyboard_module = true;
};
class TDisplayP4Board
{
  public:
    bool ready = false;
    uint8_t brightness = 0;
    static TDisplayP4Board& instance()
    {
        static TDisplayP4Board board;
        return board;
    }
    static const BoardProfile& profile()
    {
        static BoardProfile profile;
        return profile;
    }
    bool ensureKeyboardLdo4Power() { return true; }
    void setKeyboardReady(bool value) { ready = value; }
    uint8_t keyboardGetBrightness() { return brightness; }
    void keyboardSetBrightness(uint8_t value) { brightness = value; }
};
} // namespace boards::t_display_p4
inline lv_group_t* app_g = nullptr;
inline lv_group_t* menu_g = nullptr;
inline void* ui_get_active_app() { return fake::app_active ? &fake::app_active : nullptr; }
inline void ui_request_exit_to_menu() { ++fake::exits; }
inline void ui_take_screenshot_to_sd() {}
namespace ui::menu_runtime
{
inline bool handleWalkieKey(char, int) { return false; }
inline bool handleShortcutKey(char key, int)
{
    if (key != 'q') return false;
    ++fake::shortcuts;
    return true;
}
} // namespace ui::menu_runtime
extern "C" inline void trail_mate_idf_note_user_activity() {}
