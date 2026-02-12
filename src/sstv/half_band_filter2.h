//  _  ___  _   _____ _     _                 
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___ 
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/    
//
// Copyright (c) Jonathan P Dawson 2023
// filename: half_band_filter2.h
// description:
// License: MIT
//

#ifndef HALF_BAND_2_H
#define HALF_BAND_2_H

#include <stdint.h>

class half_band_filter2
{
    private:
    static const uint8_t buf_size = 64u;
    int16_t bufi[buf_size] = {0};
    int16_t bufq[buf_size] = {0};
    uint8_t pointer = 0;
    public:
    half_band_filter2();
    void filter(int16_t &i, int16_t &q);

};

#endif
