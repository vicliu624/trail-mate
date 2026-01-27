#pragma once

#include <Arduino.h>

// 抽象基类：为不同硬件板子提供统一接口。
// 仅保留当前应用层实际使用的最小集合，避免过度耦合。
class BoardBase
{
  public:
    virtual ~BoardBase() = default;

    // 生命周期 / 功耗
    virtual uint32_t begin(uint32_t disable_hw_init = 0) = 0;
    virtual void wakeUp() = 0;
    virtual void handlePowerButton() = 0;
    virtual void softwareShutdown() = 0;

    // 显示 / 亮度
    virtual void setBrightness(uint8_t level) = 0;
    virtual uint8_t getBrightness() = 0;

    // 键盘背光（如无键盘可为空实现）
    virtual bool hasKeyboard() = 0;
    virtual void keyboardSetBrightness(uint8_t level) = 0;
    virtual uint8_t keyboardGetBrightness() = 0;

    // 传感与电源状态
    virtual bool isRTCReady() const = 0;
    virtual bool isCharging() = 0;
    virtual int getBatteryLevel() = 0;

    // 存储 / 外设状态
    virtual bool isSDReady() const = 0;
    virtual bool isCardReady() = 0;
    virtual bool isGPSReady() const = 0;

    // 触觉反馈
    virtual void vibrator() = 0;
    virtual void stopVibrator() = 0;
};

// 全局板实例（与原来的 instance 等价，用于解耦调用方类型）

#ifndef DEVICE_MAX_BRIGHTNESS_LEVEL
#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
#endif
#ifndef DEVICE_MIN_BRIGHTNESS_LEVEL
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#endif

extern BoardBase& board;
