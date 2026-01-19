#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <SPI.h>
#include <stdint.h>

enum DriverBusType
{
    SPI_DRIVER,
};

typedef struct
{
    uint8_t madCmd;
    uint16_t width;
    uint16_t height;
    uint16_t offset_x;
    uint16_t offset_y;
} DispRotationConfig_t;

typedef struct
{
    uint8_t cmd;
    uint8_t data[15];
    uint8_t len;
} CommandTable_t;

typedef enum RotaryDir
{
    ROTARY_DIR_NONE,
    ROTARY_DIR_UP,
    ROTARY_DIR_DOWN,
} RotaryDir_t;

typedef enum KeyboardState
{
    KEYBOARD_RELEASED,
    KEYBOARD_PRESSED,
} KeyboardState_t;

typedef struct RotaryMsg
{
    RotaryDir_t dir;
    bool centerBtnPressed;
} RotaryMsg_t;

class LilyGo_Display
{
  public:
    LilyGo_Display(DriverBusType type, bool full_refresh) : _offset_x(0), _offset_y(0), _rotation(0), _interface(type), _full_refresh(full_refresh) {}
    virtual void setRotation(uint8_t rotation) = 0;
    virtual uint8_t getRotation() = 0;
    virtual void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color) = 0;
    virtual uint16_t width() = 0;
    virtual uint16_t height() = 0;

    virtual RotaryMsg_t getRotary()
    {
        RotaryMsg_t msg;
        return msg;
    }
    virtual uint8_t getPoint(int16_t* x, int16_t* y, uint8_t get_point) { return 0; }
    virtual int getKeyChar(char* c) { return -1; }
    virtual bool hasTouch() { return false; }
    virtual bool hasEncoder() { return false; }
    virtual bool hasKeyboard() { return false; }
    virtual void feedback(void* args = NULL) { (void)args; }
    bool needFullRefresh() { return _full_refresh; }
    virtual bool useDMA() { return false; }

  protected:
    uint16_t _offset_x;
    uint16_t _offset_y;
    uint8_t _rotation;
    DriverBusType _interface;
    bool _full_refresh;
    bool _useDMA;
};

class LilyGoDispArduinoSPI
{
  private:
    SPIClass* _spi = nullptr;
    int _cs = -1;
    int _dc = -1;
    int _backlight = -1;
    uint32_t _spi_freq = 40 * 1000U * 1000U;
    uint16_t _offset_x = 0;
    uint16_t _offset_y = 0;
    uint8_t _rotation = 0;

    uint16_t _init_width = 0;
    uint16_t _init_height = 0;
    const CommandTable_t* _init_list;
    size_t _init_list_length;
    const DispRotationConfig_t* _rotation_configs;
    SemaphoreHandle_t _lock = nullptr;

  public:
    uint16_t _width = 0;
    uint16_t _height = 0;
    uint8_t _brightness = 0;

    LilyGoDispArduinoSPI(uint16_t width, uint16_t height, const CommandTable_t* init_list,
                         size_t init_list_length, const DispRotationConfig_t* rotation_config) : _init_width(width), _init_height(height), _init_list(init_list),
                                                                                                 _init_list_length(init_list_length), _rotation_configs(rotation_config) {}

    bool init(int sck, int miso, int mosi, int cs, int rst, int dc, int backlight,
              uint32_t freq_Mhz = 80, SPIClass& spi = SPI);
    void end();
    void setRotation(uint8_t rotation);
    uint8_t getRotation();
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color);
    void pushColors(uint16_t* data, uint32_t len);
    void sleep();
    void wakeup();
    void setBrightness(uint8_t level);
    void writeParams(uint8_t cmd, uint8_t* data = nullptr, size_t length = 0);
    void writeData(uint8_t data);
    void writeCommand(uint8_t cmd);
    void setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
    bool lock(TickType_t xTicksToWait = portMAX_DELAY);
    void unlock();
};
