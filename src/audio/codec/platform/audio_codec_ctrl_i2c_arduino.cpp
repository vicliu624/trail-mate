/**
 * @file      audio_codec_ctrl_i2c_arduino.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-02
 * 
 */
#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>

#include "../interface/audio_codec_ctrl_if.h"
#include "../include/esp_codec_dev_defaults.h"

typedef struct {
    audio_codec_ctrl_if_t   base;
    bool                    is_open;
    uint8_t                 addr;
    TwoWire                *wire;
} i2c_ctrl_t;

static int _i2c_ctrl_open(const audio_codec_ctrl_if_t *ctrl, void *cfg, int cfg_size)
{
    if (ctrl == NULL || cfg == NULL || cfg_size != sizeof(audio_codec_i2c_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *) ctrl;
    audio_codec_i2c_cfg_t *i2c_cfg = (audio_codec_i2c_cfg_t *) cfg;
    if (i2c_cfg->bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_ctrl->addr = i2c_cfg->addr;
    i2c_ctrl->wire = (TwoWire*)i2c_cfg->bus_handle;
    i2c_ctrl->wire->begin();
    i2c_ctrl->wire->beginTransmission(i2c_ctrl->addr);
    uint8_t ret = i2c_ctrl->wire->endTransmission();
    return  ret == 0 ? ESP_OK : ESP_FAIL;
}

static bool _i2c_ctrl_is_open(const audio_codec_ctrl_if_t *ctrl)
{
    if (ctrl) {
        i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *) ctrl;
        return i2c_ctrl->is_open;
    }
    return false;
}

static int _i2c_ctrl_read_reg(const audio_codec_ctrl_if_t *ctrl, int addr, int addr_len, void *data, int data_len)
{
    if (ctrl == NULL || data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *) ctrl;
    if (i2c_ctrl->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    esp_err_t ret = ESP_OK;
    i2c_ctrl->wire->beginTransmission(i2c_ctrl->addr);
    i2c_ctrl->wire->write(addr);
    i2c_ctrl->wire->endTransmission();
    i2c_ctrl->wire->requestFrom(i2c_ctrl->addr, (size_t)data_len);
    i2c_ctrl->wire->readBytes((uint8_t*)data, data_len);
    return ret ? ESP_CODEC_DEV_READ_FAIL : ESP_CODEC_DEV_OK;
}

static int _i2c_ctrl_write_reg(const audio_codec_ctrl_if_t *ctrl, int addr, int addr_len, void *data, int data_len)
{
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *) ctrl;
    if (ctrl == NULL || data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (i2c_ctrl->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    esp_err_t ret = ESP_OK;
    i2c_ctrl->wire->beginTransmission(i2c_ctrl->addr);
    i2c_ctrl->wire->write(addr);
    i2c_ctrl->wire->write((uint8_t*)data, data_len);
    return i2c_ctrl->wire->endTransmission();
    return ret ? ESP_CODEC_DEV_WRITE_FAIL : ESP_CODEC_DEV_OK;
}

static int _i2c_ctrl_close(const audio_codec_ctrl_if_t *ctrl)
{
    if (ctrl == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *) ctrl;
    i2c_ctrl->is_open = false;
    return 0;
}

extern "C" const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *i2c_cfg)
{
    if (i2c_cfg == NULL) {
        log_e("Bad configuration");
        return NULL;
    }
    i2c_ctrl_t *ctrl = (i2c_ctrl_t*)calloc(1, sizeof(i2c_ctrl_t));
    if (ctrl == NULL) {
        log_e("No memory for instance");
        return NULL;
    }
    ctrl->base.open = _i2c_ctrl_open;
    ctrl->base.is_open = _i2c_ctrl_is_open;
    ctrl->base.read_reg = _i2c_ctrl_read_reg;
    ctrl->base.write_reg = _i2c_ctrl_write_reg;
    ctrl->base.close = _i2c_ctrl_close;
    int ret = _i2c_ctrl_open(&ctrl->base, i2c_cfg, sizeof(audio_codec_i2c_cfg_t));
    if (ret != 0) {
        free(ctrl);
        return NULL;
    }
    ctrl->is_open = true;
    return &ctrl->base;
}

#endif

