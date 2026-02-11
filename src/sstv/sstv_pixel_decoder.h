#pragma once

#include <cstdint>

#include "sstv_types.h"

void pixel_decoder_init();
void pixel_decoder_reset();
void pixel_decoder_set_mode(const VisModeInfo& info);
void pixel_decoder_set_audio_level(float level);
float pixel_decoder_audio_level();
void pixel_decoder_clear_frame();
void pixel_decoder_clear_accum();
void pixel_decoder_clear_pd_state();
void pixel_decoder_clear_robot_chroma();
void pixel_decoder_start_frame();
bool pixel_decoder_on_sync(bool was_receiving);
void pixel_decoder_step(int32_t mono);
bool pixel_decoder_take_frame_done();
bool pixel_decoder_is_idle();

VisMode pixel_decoder_mode();
int pixel_decoder_line_index();
int pixel_decoder_line_count();

const uint16_t* pixel_decoder_framebuffer();
uint16_t pixel_decoder_frame_width();
uint16_t pixel_decoder_frame_height();
