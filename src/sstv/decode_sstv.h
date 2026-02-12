#ifndef __SSTV_DECODER_H__
#define __SSTV_DECODER_H__

#include "half_band_filter2.h"

enum e_mode 
{
  martin_m1, 
  martin_m2, 
  scottie_s1, 
  scottie_s2,
  scottie_dx,
  pd_50,
  pd_90,
  pd_120,
  pd_180,
  sc2_60,
  sc2_120,
  sc2_180,
  robot24,
  robot36,
  robot72,
  bw8,
  bw12,
  num_modes
};

enum e_sync_state
{
  detect,
  confirm,
};

enum e_state
{
  detect_sync,
  confirm_sync,
  decode_line,
  wait,
};

struct s_sstv_mode 
{
  uint16_t width;
  uint16_t max_height;
  uint32_t samples_per_line;
  uint32_t samples_per_colour_line;
  uint32_t samples_per_colour_gap;
  uint32_t samples_per_pixel;
  uint32_t samples_per_hsync;
};

class c_sstv_decoder
{

  private:

  s_sstv_mode sstv_mode;
  float m_Fs;
  uint32_t m_scale;
  uint32_t sync_counter = 0;
  uint16_t y_pixel = 0;
  uint16_t last_x = 0;
  uint32_t image_sample = 0;
  uint16_t last_sample = 0;
  uint32_t last_hsync_sample = 0;
  uint32_t sample_number = 0;
  uint32_t confirmed_sync_sample = 0;
  e_state state = detect_sync;
  e_sync_state sync_state = detect;
  void sample_to_pixel(uint16_t &x, uint16_t &y, uint8_t &colour, int32_t image_sample);
  uint8_t frequency_to_brightness(uint16_t x);
  uint32_t mean_samples_per_line;
  uint32_t sync_timeout = 0;
  uint32_t confirm_count;
  uint32_t pixel_accumulator;
  uint16_t pixel_n;
  int16_t last_phase = 0;
  uint8_t ssb_phase = 0;
  half_band_filter2 ssb_filter;
  int16_t frequency;
  e_mode decode_mode;
  s_sstv_mode modes[num_modes];
  bool m_auto_slant_correction;
  uint32_t m_timeout;

  public:
  c_sstv_decoder(float Fs);
  bool decode(uint16_t sample, uint16_t &line, uint16_t &col, uint8_t &colour, uint8_t &pixel, e_state &debug_state);
  bool decode_iq(int16_t sample_i, int16_t sample_q, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &frequency);
  bool decode_audio(int16_t audio, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &frequency);
  void set_timeout_seconds(uint8_t timeout);
  void set_auto_slant_correction(bool enable);
  e_mode get_mode();
  s_sstv_mode * get_modes();

  
};

#endif
