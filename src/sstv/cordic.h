//  _  ___  _   _____ _     _                 
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___ 
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/    
//
// Copyright (c) Jonathan P Dawson 2023
// filename: cordic.h
// description:
// License: MIT
//

#ifndef CORDIC_H__
#define CORDIC_H__

#include <cstdint>

void cordic_init();
void cordic_rectangular_to_polar(int16_t i, int16_t q, uint16_t &magnitude, int16_t &phase);

#endif
