//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2023
// filename: half_band_filter2.cpp
// description:
// License: MIT
//

#include "half_band_filter2.h"
#include <cstdio>

half_band_filter2 ::half_band_filter2() {
  pointer = 0;
  for (uint8_t tap = 0; tap < buf_size; tap++) {
    bufi[tap] = 0;
    bufq[tap] = 0;
  }
}

void half_band_filter2 ::filter(int16_t &i, int16_t &q) {

  // filter kernel:
  // 0, 0, 1, 0, -6, 0, 16, 0, -32, 0, 60, 0, -102, 0, 164, 0, -254, 0, 381, 0,
  // -561, 0, 818, 0, -1209, 0, 1876, 0, -3347, 0, 10387, 16384, 10387, 0,
  // -3347, 0, 1876, 0, -1209, 0, 818, 0, -561, 0, 381, 0, -254, 0, 164, 0,
  // -102, 0, 60, 0, -32, 0, 16, 0, -6, 0, 1, 0, 0

  bufi[pointer] = i;
  bufq[pointer] = q;
  pointer++;
  pointer &= 0x3f;
  const uint8_t idx2 = (pointer + 2) & 0x3f;
  const uint8_t idx4 = (pointer + 4) & 0x3f;
  const uint8_t idx6 = (pointer + 6) & 0x3f;
  const uint8_t idx8 = (pointer + 8) & 0x3f;
  const uint8_t idx10 = (pointer + 10) & 0x3f;
  const uint8_t idx12 = (pointer + 12) & 0x3f;
  const uint8_t idx14 = (pointer + 14) & 0x3f;
  const uint8_t idx16 = (pointer + 16) & 0x3f;
  const uint8_t idx18 = (pointer + 18) & 0x3f;
  const uint8_t idx20 = (pointer + 20) & 0x3f;
  const uint8_t idx22 = (pointer + 22) & 0x3f;
  const uint8_t idx24 = (pointer + 24) & 0x3f;
  const uint8_t idx26 = (pointer + 26) & 0x3f;
  const uint8_t idx28 = (pointer + 28) & 0x3f;
  const uint8_t idx30 = (pointer + 30) & 0x3f;
  const uint8_t idx31 = (pointer + 31) & 0x3f;
  const uint8_t idx32 = (pointer + 32) & 0x3f;
  const uint8_t idx34 = (pointer + 34) & 0x3f;
  const uint8_t idx36 = (pointer + 36) & 0x3f;
  const uint8_t idx38 = (pointer + 38) & 0x3f;
  const uint8_t idx40 = (pointer + 40) & 0x3f;
  const uint8_t idx42 = (pointer + 42) & 0x3f;
  const uint8_t idx44 = (pointer + 44) & 0x3f;
  const uint8_t idx46 = (pointer + 46) & 0x3f;
  const uint8_t idx48 = (pointer + 48) & 0x3f;
  const uint8_t idx50 = (pointer + 50) & 0x3f;
  const uint8_t idx52 = (pointer + 52) & 0x3f;
  const uint8_t idx54 = (pointer + 54) & 0x3f;
  const uint8_t idx56 = (pointer + 56) & 0x3f;
  const uint8_t idx58 = (pointer + 58) & 0x3f;
  const uint8_t idx60 = (pointer + 60) & 0x3f;
  i = (
          ((static_cast<int32_t>(bufi[idx2]) + static_cast<int32_t>(bufi[idx60])) * 1) +
          ((static_cast<int32_t>(bufi[idx4]) + static_cast<int32_t>(bufi[idx58])) * -6) +
          ((static_cast<int32_t>(bufi[idx6]) + static_cast<int32_t>(bufi[idx56])) * 16) +
          ((static_cast<int32_t>(bufi[idx8]) + static_cast<int32_t>(bufi[idx54])) * -32) +
          ((static_cast<int32_t>(bufi[idx10]) + static_cast<int32_t>(bufi[idx52])) * 60) +
          ((static_cast<int32_t>(bufi[idx12]) + static_cast<int32_t>(bufi[idx50])) * -102) +
          ((static_cast<int32_t>(bufi[idx14]) + static_cast<int32_t>(bufi[idx48])) * 164) +
          ((static_cast<int32_t>(bufi[idx16]) + static_cast<int32_t>(bufi[idx46])) * -254) +
          ((static_cast<int32_t>(bufi[idx18]) + static_cast<int32_t>(bufi[idx44])) * 381) +
          ((static_cast<int32_t>(bufi[idx20]) + static_cast<int32_t>(bufi[idx42])) * -561) +
          ((static_cast<int32_t>(bufi[idx22]) + static_cast<int32_t>(bufi[idx40])) * 818) +
          ((static_cast<int32_t>(bufi[idx24]) + static_cast<int32_t>(bufi[idx38])) * -1209) +
          ((static_cast<int32_t>(bufi[idx26]) + static_cast<int32_t>(bufi[idx36])) * 1876) +
          ((static_cast<int32_t>(bufi[idx28]) + static_cast<int32_t>(bufi[idx34])) * -3347) +
          ((static_cast<int32_t>(bufi[idx30]) + static_cast<int32_t>(bufi[idx32])) * 10387) +
          static_cast<int32_t>(bufi[idx31]) * 16384) >> 15;

  q = (
          ((static_cast<int32_t>(bufq[idx2]) + static_cast<int32_t>(bufq[idx60])) * 1) +
          ((static_cast<int32_t>(bufq[idx4]) + static_cast<int32_t>(bufq[idx58])) * -6) +
          ((static_cast<int32_t>(bufq[idx6]) + static_cast<int32_t>(bufq[idx56])) * 16) +
          ((static_cast<int32_t>(bufq[idx8]) + static_cast<int32_t>(bufq[idx54])) * -32) +
          ((static_cast<int32_t>(bufq[idx10]) + static_cast<int32_t>(bufq[idx52])) * 60) +
          ((static_cast<int32_t>(bufq[idx12]) + static_cast<int32_t>(bufq[idx50])) * -102) +
          ((static_cast<int32_t>(bufq[idx14]) + static_cast<int32_t>(bufq[idx48])) * 164) +
          ((static_cast<int32_t>(bufq[idx16]) + static_cast<int32_t>(bufq[idx46])) * -254) +
          ((static_cast<int32_t>(bufq[idx18]) + static_cast<int32_t>(bufq[idx44])) * 381) +
          ((static_cast<int32_t>(bufq[idx20]) + static_cast<int32_t>(bufq[idx42])) * -561) +
          ((static_cast<int32_t>(bufq[idx22]) + static_cast<int32_t>(bufq[idx40])) * 818) +
          ((static_cast<int32_t>(bufq[idx24]) + static_cast<int32_t>(bufq[idx38])) * -1209) +
          ((static_cast<int32_t>(bufq[idx26]) + static_cast<int32_t>(bufq[idx36])) * 1876) +
          ((static_cast<int32_t>(bufq[idx28]) + static_cast<int32_t>(bufq[idx34])) * -3347) +
          ((static_cast<int32_t>(bufq[idx30]) + static_cast<int32_t>(bufq[idx32])) * 10387) +
          static_cast<int32_t>(bufq[idx31]) * 16384) >> 15;

}
