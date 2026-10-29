#pragma once
#include "freertos/FreeRTOS.h"
#include "vendor.h"
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_TIMEOUT = 0x107;
constexpr int ESP_ERR_NOT_FOUND = 0x105;
constexpr int ESP_ERR_INVALID_STATE = 0x103;
inline esp_err_t i2c_master_probe(cpp_bus_driver::HardwareI2c1* bus, uint16_t address, int timeout)
{
    fake::probe_addresses.push_back(address);
    const bool ack = bus->Probe(address);
    // IDF waits for an asynchronous completion interrupt using this truncated
    // tick count. An available device does not make a zero-tick wait valid.
    if (fake::probe_timeout || pdMS_TO_TICKS(timeout) == 0) return ESP_ERR_TIMEOUT;
    return ack ? ESP_OK : ESP_ERR_NOT_FOUND;
}
