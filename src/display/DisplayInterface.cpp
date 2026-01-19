#include "display/DisplayInterface.h"
#include <Arduino.h>
#include <vector>

#define DISP_CMD_MADCTL (0x36)
#define DISP_CMD_CASET (0x2A)
#define DISP_CMD_RASET (0x2B)
#define DISP_CMD_RAMWR (0x2C)
#define DISP_CMD_SLPIN (0x10)
#define DISP_CMD_SLPOUT (0x11)

bool LilyGoDispArduinoSPI::lock(TickType_t xTicksToWait)
{
    return xSemaphoreTake(_lock, xTicksToWait) == pdTRUE;
}

void LilyGoDispArduinoSPI::unlock()
{
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::setBrightness(uint8_t level)
{
    _brightness = level;
}

bool LilyGoDispArduinoSPI::init(int sck,
                                int miso,
                                int mosi,
                                int cs,
                                int rst,
                                int dc,
                                int backlight,
                                uint32_t freq_Mhz,
                                SPIClass& spi)
{
    _lock = xSemaphoreCreateMutex();
    _spi = &spi;

    if (rst != -1)
    {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, LOW);
        delay(20);
        digitalWrite(rst, HIGH);
        delay(120);
    }
    _width = _init_width;
    _height = _init_height;

    _cs = cs;
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    _dc = dc;
    pinMode(_dc, OUTPUT);
    digitalWrite(_dc, HIGH);

    _backlight = backlight;
    if (_backlight != -1)
    {
        pinMode(_backlight, OUTPUT);
        digitalWrite(_backlight, HIGH);
    }

    _spi->begin(sck, miso, mosi);

    for (uint32_t i = 0; i < _init_list_length; i++)
    {
        writeParams(_init_list[i].cmd, (uint8_t*)_init_list[i].data, _init_list[i].len & 0x1F);
        if (_init_list[i].len & 0x80)
        {
            delay(120);
        }
    }

    setRotation(0);

    _spi_freq = freq_Mhz * 1000U * 1000U;

    std::vector<uint16_t> draw_buf(_width * _height * 2, 0x0000);
    pushColors(0, 0, _width, _height, draw_buf.data());
    xSemaphoreGive(_lock);
    return true;
}

void LilyGoDispArduinoSPI::end()
{
    // Shared bus, no deinit
}

uint8_t LilyGoDispArduinoSPI::getRotation()
{
    return _rotation;
}

void LilyGoDispArduinoSPI::setRotation(uint8_t rotation)
{
    _rotation = rotation % 4;
    writeCommand(DISP_CMD_MADCTL);
    writeData(_rotation_configs[_rotation].madCmd);
    _width = _rotation_configs[_rotation].width;
    _height = _rotation_configs[_rotation].height;
    _offset_x = _rotation_configs[_rotation].offset_x;
    _offset_y = _rotation_configs[_rotation].offset_y;
}

void LilyGoDispArduinoSPI::pushColors(uint16_t* data, uint32_t len)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, HIGH);
    _spi->writeBytes((const uint8_t*)data, len * sizeof(uint16_t));
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    setAddrWindow(x1, y1, x1 + x2 - 1, y1 + y2 - 1);
    pushColors(color, x2 * y2);
}

void LilyGoDispArduinoSPI::sleep()
{
    writeCommand(DISP_CMD_SLPIN);
}

void LilyGoDispArduinoSPI::wakeup()
{
    writeCommand(DISP_CMD_SLPOUT);
}

void LilyGoDispArduinoSPI::setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    xs += _offset_x;
    ys += _offset_y;
    xe += _offset_x;
    ye += _offset_y;
    CommandTable_t t[3] = {
        {DISP_CMD_CASET, {uint8_t(xs >> 8), (uint8_t)xs, uint8_t(xe >> 8), uint8_t(xe)}, 0x04},
        {DISP_CMD_RASET, {uint8_t(ys >> 8), (uint8_t)ys, uint8_t(ye >> 8), uint8_t(ye)}, 0x04},
        {DISP_CMD_RAMWR, {0x00}, 0x00},
    };
    for (uint32_t i = 0; i < 3; i++)
    {
        writeParams(t[i].cmd, t[i].data, t[i].len);
    }
}

void LilyGoDispArduinoSPI::writeCommand(uint8_t cmd)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, LOW);
    _spi->write(cmd);
    digitalWrite(_dc, HIGH);
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::writeData(uint8_t data)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, HIGH);
    _spi->write(data);
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::writeParams(uint8_t cmd, uint8_t* data, size_t length)
{
    writeCommand(cmd);
    for (size_t i = 0; i < length; i++)
    {
        writeData(data[i]);
    }
}
