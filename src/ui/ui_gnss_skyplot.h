#pragma once

#include "lvgl.h"

struct SatInfo
{
    enum Sys
    {
        GPS,
        GLN,
        GAL,
        BD
    } sys = GPS;

    enum SNRState
    {
        GOOD,
        FAIR,
        WEAK,
        NOT_USED,
        IN_VIEW
    } snr_state = IN_VIEW;

    int id = 0;            // PRN/SVID
    float azimuth = 0.0;   // 0..359 deg
    float elevation = 0.0; // 0..90 deg
    int snr = 0;           // dB-Hz
    bool used = false;
};

struct GnssStatus
{
    enum Fix
    {
        NOFIX,
        FIX2D,
        FIX3D
    } fix = NOFIX;

    int sats_in_use = 0;
    int sats_in_view = 0;
    float hdop = 0.0f;
};

lv_obj_t* ui_gnss_skyplot_create(lv_obj_t* parent);
void ui_gnss_skyplot_set_sats(const SatInfo* sats, int count);
void ui_gnss_skyplot_set_status(GnssStatus st);

void ui_gnss_skyplot_enter(lv_obj_t* parent);
void ui_gnss_skyplot_exit(lv_obj_t* parent);
