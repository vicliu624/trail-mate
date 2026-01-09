/**
 * @file      GPS.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-07
 *
 */
#pragma once

#include <Arduino.h>
#include <TinyGPSPlus.h>

class GPS : public TinyGPSPlus
{
public:
    GPS();

    ~GPS();

    bool init(Stream *stream);
    bool factory();

    uint32_t  loop(bool debug = false)
    {
        while (_stream->available()) {
            int c = _stream->read();
            if (debug) {
                Serial.write(c);
            } else {
                encode(c);
            }
        }
        if (debug) {
            while (Serial.available()) {
                _stream->write(Serial.read());
            }
        }

        return charsProcessed();
    }

    String getModel()
    {
        return model;
    }
private:
    int getAck(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID);
    Stream *_stream;
    String model;
};
