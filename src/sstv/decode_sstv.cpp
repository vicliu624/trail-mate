#include <cstdint>
#include <algorithm>
#include <cmath>
#include "decode_sstv.h"
#include "cordic.h"

//from the sample number work out the colour and x/y coordinates
void c_sstv_decoder :: sample_to_pixel(uint16_t &x, uint16_t &y, uint8_t &colour, int32_t image_sample)
{
  //martin and scottie colour order is g-b-r, map to r-g-b
  static const uint8_t colourmap[4] = {1, 2, 0, 4};



  if( decode_mode == martin_m1 || decode_mode == martin_m2 )
  {
    //approximate delay of sync detection
    image_sample += m_scale*m_Fs*1.25/1000.0;
    
    image_sample -= modes[decode_mode].samples_per_hsync;
    if(image_sample < 0)
    {
      //return colour 4 for non-displayable pixels (e.g. during hsync)
      x = 0; y=0; colour=4;
      return;
    }
    y = image_sample/mean_samples_per_line;
    image_sample -= y*mean_samples_per_line;
    colour = image_sample/modes[decode_mode].samples_per_colour_line;
    image_sample -= colour*modes[decode_mode].samples_per_colour_line;
    colour = colourmap[colour];
    x = image_sample/modes[decode_mode].samples_per_pixel;
    
  }

  else if(decode_mode == robot36)
  {
    image_sample -= modes[decode_mode].samples_per_hsync;
    if(image_sample < 0)
    {
      //return colour 4 for non-displayable pixels (e.g. during hsync)
      x = 0; y=0; colour=4;
      return;
    }
    y = image_sample/mean_samples_per_line;
    image_sample -= y*mean_samples_per_line;
   
    //Double duration of y channel
    if (image_sample<modes[decode_mode].samples_per_colour_line*2)
    {
      colour = 0; 
      x = image_sample/(modes[decode_mode].samples_per_pixel*2);
    } else if (image_sample<modes[decode_mode].samples_per_colour_line*2+modes[robot36].samples_per_colour_gap) {
      //For detecting 2300 or 1500 sync
      colour=3;
      x=(image_sample-modes[decode_mode].samples_per_colour_line*2)/modes[decode_mode].samples_per_pixel;
    } else if (image_sample>modes[decode_mode].samples_per_colour_line*2+modes[robot36].samples_per_colour_gap) {
      //Alternatively channel 1 (cr) and 2 (cb)    
      colour = 1+(y%2);
      image_sample -=modes[decode_mode].samples_per_colour_line*2+modes[robot36].samples_per_colour_gap; 
      x = image_sample/(modes[decode_mode].samples_per_pixel);
    } 
  }

//robot24
//200ms
//sync y    1500 r-y  2300 b-y
//12   88   6    44   6    44

//robot72
//300ms
//sync y    1500 r-y  2300 b-y
//12   138  6    69   6    69

  else if(decode_mode == robot24 || decode_mode == robot72)
  {

    //approximate delay of sync detection
    image_sample += m_scale*m_Fs*1.25/1000.0;

    y = image_sample/mean_samples_per_line;
    image_sample -= y*mean_samples_per_line;

    uint32_t samples_per_colour = modes[decode_mode].samples_per_colour_line + modes[decode_mode].samples_per_hsync;

    if(image_sample < 2*samples_per_colour)
    { 
      colour = 0;
      image_sample -= 2*modes[decode_mode].samples_per_hsync;
      x = image_sample/(2*modes[decode_mode].samples_per_pixel);
    }
    else if(image_sample < 3*samples_per_colour)
    { 
      colour = 1;
      image_sample -= modes[decode_mode].samples_per_hsync;
      image_sample -= 2*samples_per_colour;
      x = image_sample/(modes[decode_mode].samples_per_pixel);
    }
    else if(image_sample < 4*samples_per_colour)
    { 
      colour = 2;
      image_sample -= modes[decode_mode].samples_per_hsync;
      image_sample -= 3*samples_per_colour;
      x = image_sample/(modes[decode_mode].samples_per_pixel);
    }
    else
    {
      colour = 4;
      x = 0;
    }
    
    if( image_sample < 0 )
    {
        //return colour 4 for non-displayable pixels (e.g. during hsync)
        x = 0; y=0; colour=4;
        return;
    }
    
  }
  
  else if(decode_mode == bw8 || decode_mode == bw12)
  {  
    y = image_sample/mean_samples_per_line;
    image_sample -= y*mean_samples_per_line;

    uint32_t samples_per_colour = modes[decode_mode].samples_per_colour_line + modes[decode_mode].samples_per_hsync;

    if(image_sample < samples_per_colour)
    { 
      colour = 0;
      image_sample -= modes[decode_mode].samples_per_hsync;
      x = image_sample/modes[decode_mode].samples_per_pixel;
    }
    else
    {
      colour = 4;
      x = 0;
    }
    
    if( image_sample < 0 )
    {
        //return colour 4 for non-displayable pixels (e.g. during hsync)
        x = 0; y=0; colour=4;
        return;
    }
    
  }
    
  else if( decode_mode == scottie_s1 || decode_mode == scottie_s2 || decode_mode == scottie_dx)
  {

    //with scottie, sync id mid-line between blue and red.
    //subtract the red period to sync to next full line
    image_sample -= modes[decode_mode].samples_per_colour_line;
    image_sample -= modes[decode_mode].samples_per_hsync;
    if(image_sample < 0)
    {
        //return colour 4 for non-displayable pixels (e.g. during hsync)
        x = 0; y=0; colour=4;
        return;
    }

    y = image_sample/mean_samples_per_line;    
    image_sample -= y*mean_samples_per_line;
    
    //hsync is between blue and red component (not at end of line)
    //for red component, subtract the length of the scan-line
    if( image_sample < 2*modes[decode_mode].samples_per_colour_line)
    {
      colour = image_sample/modes[decode_mode].samples_per_colour_line;
      image_sample -= colour*modes[decode_mode].samples_per_colour_line;
    }
    else
    {
      image_sample -= 2*modes[decode_mode].samples_per_colour_line;
      image_sample -= modes[decode_mode].samples_per_hsync;
      colour = 2 + (image_sample/modes[decode_mode].samples_per_colour_line);
    }
    if( image_sample < 0 )
    {
        //return colour 4 for non-displayable pixels (e.g. during hsync)
        x=0; y=0; colour=4;
        return;
    }

    colour = colourmap[colour]; //scottie colour order is g-b-r, map to r-g-b
    x = image_sample/modes[decode_mode].samples_per_pixel;

  }

  else if( decode_mode == pd_50 || decode_mode == pd_90 || decode_mode == pd_120 || decode_mode == pd_180)
  {
    static const uint8_t colourmap[5] = {0, 1, 2, 3, 4};

    image_sample -= modes[decode_mode].samples_per_hsync;
    if(image_sample < 0)
    {
      //return colour 4 for non-displayable pixels (e.g. during hsync)
      x = 0; y=0; colour=4;
      return;
    }
    y = image_sample/mean_samples_per_line;
    image_sample -= y*mean_samples_per_line;
    colour = image_sample/modes[decode_mode].samples_per_colour_line;
    image_sample -= colour*modes[decode_mode].samples_per_colour_line;
    colour = colourmap[colour];
    x = image_sample/modes[decode_mode].samples_per_pixel;
  }

  else if( decode_mode == sc2_60 || decode_mode == sc2_120 || decode_mode == sc2_180)
  {

    //approximate delay of sync detection
    image_sample += m_scale*m_Fs*1.25/1000.0;

    image_sample -= modes[decode_mode].samples_per_hsync;
    if(image_sample < 0)
    {
      //return colour 4 for non-displayable pixels (e.g. during hsync)
      x = 0; y=0; colour=4;
      return;
    }

    y = image_sample/mean_samples_per_line;
    image_sample -= y*mean_samples_per_line;

    if( image_sample < modes[decode_mode].samples_per_colour_line )
    {
      colour = 0;
      x = image_sample/modes[decode_mode].samples_per_pixel;
    }
    else if( image_sample < 2*modes[decode_mode].samples_per_colour_line )
    {
      colour = 1;
      image_sample -= modes[decode_mode].samples_per_colour_line;
      x = image_sample/modes[decode_mode].samples_per_pixel;
    }
    else if( image_sample < 3*modes[decode_mode].samples_per_colour_line )
    {
      colour = 2;
      image_sample -= 2*modes[decode_mode].samples_per_colour_line;
      x = image_sample/modes[decode_mode].samples_per_pixel;
    }
    else
    {
      colour = 4;
      x = 0;
    }

    if(image_sample < 0)
    {
      //return colour 4 for non-displayable pixels (e.g. during hsync)
      x = 0; y=0; colour=4;
      return;
    }
  }
}

uint8_t c_sstv_decoder :: frequency_to_brightness(uint16_t x)
{
  int16_t brightness = (256*(x-1500))/(2300-1500);
  return std::min(std::max(brightness, (int16_t)0), (int16_t)255);
}

bool parity_check(uint8_t x)
{
  x ^= x >> 4;
  x ^= x >> 2;
  x ^= x >> 1;
  return (~x) & 1;
}

c_sstv_decoder :: c_sstv_decoder(float Fs)
{

  m_Fs = Fs;
  static const uint32_t fraction_bits = 8;
  static const uint32_t scale = 1<<fraction_bits;
  m_scale = scale;

  m_auto_slant_correction = true;
  m_timeout = m_Fs*30;

  //martin m1
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 4.862;
  const float colour_gap_ms = 0.572;
  const float colour_time_ms = 146.342;
  modes[martin_m1].width = width;
  modes[martin_m1].samples_per_line = scale*Fs*((colour_time_ms*3)+(colour_gap_ms*4) + hsync_pulse_ms)/1000.0;
  modes[martin_m1].samples_per_colour_line = scale*Fs*(colour_time_ms+colour_gap_ms)/1000.0;
  modes[martin_m1].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[martin_m1].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[martin_m1].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[martin_m1].max_height = 256;
  }

  //martin m2
  {
  const uint16_t width = 160;
  const float hsync_pulse_ms = 4.862;
  const float colour_gap_ms = 0.572;
  const float colour_time_ms = 73.216;
  modes[martin_m2].width = width;
  modes[martin_m2].samples_per_line = scale*Fs*((colour_time_ms*3)+(colour_gap_ms*4) + hsync_pulse_ms)/1000.0;
  modes[martin_m2].samples_per_colour_line = scale*Fs*(colour_time_ms+colour_gap_ms)/1000.0;
  modes[martin_m2].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[martin_m2].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[martin_m2].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[martin_m2].max_height = 256;
  }

  //scottie s1
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 9;
  const float colour_gap_ms = 1.5;
  const float colour_time_ms = 138.240;
  modes[scottie_s1].width = width;
  modes[scottie_s1].samples_per_line = scale*Fs*((colour_time_ms*3)+(colour_gap_ms*3) + hsync_pulse_ms)/1000.0;
  modes[scottie_s1].samples_per_colour_line = scale*Fs*(colour_time_ms+colour_gap_ms)/1000.0;
  modes[scottie_s1].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[scottie_s1].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[scottie_s1].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[scottie_s1].max_height = 256;
  }

  //scottie s2
  {
  const uint16_t width = 160;
  const float hsync_pulse_ms = 9;
  const float colour_gap_ms = 1.5;
  const float colour_time_ms = 88.064;
  modes[scottie_s2].width = width;
  modes[scottie_s2].samples_per_line = scale*Fs*((colour_time_ms*3)+(colour_gap_ms*3) + hsync_pulse_ms)/1000.0;
  modes[scottie_s2].samples_per_colour_line = scale*Fs*(colour_time_ms+colour_gap_ms)/1000.0;
  modes[scottie_s2].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[scottie_s2].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[scottie_s2].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[scottie_s2].max_height = 256;
  }
  
  //scottie dx
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 9;
  const float colour_gap_ms = 1.5;
  const float colour_time_ms = 345.600;
  modes[scottie_dx].width = width;
  modes[scottie_dx].samples_per_line = scale*Fs*((colour_time_ms*3)+(colour_gap_ms*3) + hsync_pulse_ms)/1000.0;
  modes[scottie_dx].samples_per_colour_line = scale*Fs*(colour_time_ms+colour_gap_ms)/1000.0;
  modes[scottie_dx].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[scottie_dx].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[scottie_dx].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[scottie_dx].max_height = 256;
  }
  
  //pd 50
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 20;
  const float colour_gap_ms = 2.08;
  const float colour_time_ms = 91.520;
  modes[pd_50].width = width;
  modes[pd_50].samples_per_line = scale*Fs*((colour_time_ms*4)+(colour_gap_ms*1) + hsync_pulse_ms)/1000.0;
  modes[pd_50].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[pd_50].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[pd_50].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[pd_50].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[pd_50].max_height = 128;
  }

  //pd 90
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 20;
  const float colour_gap_ms = 2.08;
  const float colour_time_ms = 170.240;
  modes[pd_90].width = width;
  modes[pd_90].samples_per_line = scale*Fs*((colour_time_ms*4)+(colour_gap_ms*1) + hsync_pulse_ms)/1000.0;
  modes[pd_90].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[pd_90].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[pd_90].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[pd_90].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[pd_90].max_height = 128;
  }

  //pd 120
  {
  const uint16_t width = 320; //use 320 rather than 640 as an easy way to scale image
  const float hsync_pulse_ms = 20;
  const float colour_gap_ms = 2.08;
  const float colour_time_ms = 121.600;
  modes[pd_120].width = width;
  modes[pd_120].samples_per_line = scale*Fs*((colour_time_ms*4)+(colour_gap_ms*1) + hsync_pulse_ms)/1000.0;
  modes[pd_120].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[pd_120].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[pd_120].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[pd_120].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[pd_120].max_height = 248;
  }

  //pd 180
  {
  const uint16_t width = 320; //use 320 rather than 640 as an easy way to scale image
  const float hsync_pulse_ms = 20;
  const float colour_gap_ms = 2.08;
  const float colour_time_ms = 183.040;
  modes[pd_180].width = width;
  modes[pd_180].samples_per_line = scale*Fs*((colour_time_ms*4)+(colour_gap_ms*1) + hsync_pulse_ms)/1000.0;
  modes[pd_180].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[pd_180].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[pd_180].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[pd_180].samples_per_hsync = scale*Fs*hsync_pulse_ms/1000.0;
  modes[pd_180].max_height = 248;
  }

  //SC260
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 5;
  const float colour_gap_ms = 0;
  const float colour_time_ms = 78.468;
  modes[sc2_60].width = width;
  modes[sc2_60].samples_per_line = scale*Fs*((colour_time_ms*3) + hsync_pulse_ms)/1000.0;
  modes[sc2_60].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[sc2_60].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[sc2_60].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[sc2_60].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[sc2_60].max_height = 256;
  }

  //SC2120
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 5;
  const float colour_gap_ms = 0;
  const float colour_time_ms = 156.852;
  modes[sc2_120].width = width;
  modes[sc2_120].samples_per_line = scale*Fs*((colour_time_ms*3) + hsync_pulse_ms)/1000.0;
  modes[sc2_120].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[sc2_120].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[sc2_120].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[sc2_120].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[sc2_120].max_height = 256;
  }

  //SC2180
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 5;
  const float colour_gap_ms = 0;
  const float colour_time_ms = 235.362;
  modes[sc2_180].width = width;
  modes[sc2_180].samples_per_line = scale*Fs*((colour_time_ms*3) + hsync_pulse_ms)/1000.0;
  modes[sc2_180].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[sc2_180].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[sc2_180].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[sc2_180].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[sc2_180].max_height = 256;
  }

  //Robot24
  {
  const uint16_t width = 160;
  const float hsync_pulse_ms = 4;
  const float colour_gap_ms = 1.5;
  const float colour_time_ms = 46;
  modes[robot24].width = width;
  modes[robot24].samples_per_line = scale*Fs*((colour_time_ms + hsync_pulse_ms) * 4)/1000.0;
  modes[robot24].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[robot24].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[robot24].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[robot24].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[robot24].max_height = 120;
  }

  //Robot36 tot:150
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 9;
  const float colour_gap_ms = 6;
  const float colour_time_ms = 44;
  modes[robot36].width = width;
  modes[robot36].samples_per_line = scale*Fs*((colour_time_ms*3)+ (colour_gap_ms*1.5) + hsync_pulse_ms)/1000.0;
  modes[robot36].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[robot36].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[robot36].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[robot36].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[robot36].max_height = 240;
  }

  //Robot72
  {
  const uint16_t width = 320;
  const float hsync_pulse_ms = 6;
  const float colour_gap_ms = 1.5;
  const float colour_time_ms = 69;
  modes[robot72].width = width;
  modes[robot72].samples_per_line = scale*Fs*((colour_time_ms + hsync_pulse_ms) * 4)/1000.0;
  modes[robot72].samples_per_colour_line = scale*Fs*(colour_time_ms)/1000.0;
  modes[robot72].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[robot72].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[robot72].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[robot72].max_height = 240;
  }

  //bw8
  {
  const uint16_t width = 160;
  const float hsync_pulse_ms = 10;
  const float colour_gap_ms = 0;
  const float colour_time_ms = 57;
  modes[bw8].width = width;
  modes[bw8].samples_per_line = scale*Fs*(colour_time_ms + hsync_pulse_ms)/1000.0;
  modes[bw8].samples_per_colour_line = scale*Fs*colour_time_ms/1000.0;
  modes[bw8].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[bw8].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[bw8].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[bw8].max_height = 120;
  }

  //bw12
  {
  const uint16_t width = 160;
  const float hsync_pulse_ms = 7;
  const float colour_gap_ms = 0;
  const float colour_time_ms = 93;
  modes[bw12].width = width;
  modes[bw12].samples_per_line = scale*Fs*(colour_time_ms + hsync_pulse_ms)/1000.0;
  modes[bw12].samples_per_colour_line = scale*Fs*colour_time_ms/1000.0;
  modes[bw12].samples_per_colour_gap = scale*Fs*colour_gap_ms/1000.0;
  modes[bw12].samples_per_pixel = scale*Fs*colour_time_ms/(1000.0 * width);
  modes[bw12].samples_per_hsync = m_scale*Fs*hsync_pulse_ms/1000.0;
  modes[bw12].max_height = 120;
  }

  cordic_init();
}

bool c_sstv_decoder :: decode_audio(int16_t audio, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &frequency)
{
    // shift frequency by +FS/4
    //       __|__
    //   ___/  |  \___
    //         |
    //   <-----+----->

    //        | ____
    //  ______|/    \
    //        |
    //  <-----+----->

    // filter -Fs/4 to +Fs/4

    //        | __
    //  ______|/  \__
    //        |
    //  <-----+----->

    ssb_phase = (ssb_phase + 1) & 3u;
    audio = audio >> 1;

    const int16_t audio_i[4] = {audio, 0, (int16_t)-audio, 0};
    const int16_t audio_q[4] = {0, (int16_t)-audio, 0, audio};
    int16_t ii = audio_i[ssb_phase];
    int16_t qq = audio_q[ssb_phase];
    ssb_filter.filter(ii, qq);

    // shift frequency by -FS/4
    //         | __
    //   ______|/  \__
    //         |
    //   <-----+----->

    //     __ |
    //  __/  \|______
    //        |
    //  <-----+----->

    const int16_t sample_i[4] = {(int16_t)-qq, (int16_t)-ii, qq, ii};
    const int16_t sample_q[4] = {ii, (int16_t)-qq, (int16_t)-ii, qq};
    int16_t i = sample_i[ssb_phase];
    int16_t q = sample_q[ssb_phase];

    return decode_iq(i, q, pixel_y, pixel_x, pixel_colour, pixel, frequency);
}


bool c_sstv_decoder :: decode_iq(int16_t sample_i, int16_t sample_q, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &smoothed_sample_16)
{

  uint16_t magnitude;
  int16_t phase;

  cordic_rectangular_to_polar(sample_i, sample_q, magnitude, phase);
  frequency = last_phase-phase;
  last_phase = phase;

  int16_t sample = (int32_t)frequency*15000>>16;


  static uint32_t smoothed_sample = 0;
  smoothed_sample = ((smoothed_sample << 3) + sample - smoothed_sample) >> 3;
  smoothed_sample_16 = std::min(std::max(smoothed_sample, (uint32_t)1000u), (uint32_t)2500u);

  e_state debug_state;
  return decode(smoothed_sample_16, pixel_y, pixel_x, pixel_colour, pixel, debug_state);

}

bool c_sstv_decoder :: decode(uint16_t sample, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, e_state &debug_state)
{


  //detect scan syncs
  bool sync_found = false;
  uint32_t line_length = 0u;
  if(sync_state == detect)
  {
    if( sample < 1300 && last_sample >= 1300)
    {
      sync_state = confirm;
      sync_counter = 0;
    }
  }
  else if(sync_state == confirm)
  {
    if( sample < 1300)
    {
      sync_counter++;
    }
    else if(sync_counter > 0)
    {
      sync_counter--;
    }

    if(sync_counter == 10)
    {
      sync_found = true;
      line_length = sample_number-last_hsync_sample;
      last_hsync_sample = sample_number;
      sync_state = detect;
    }
  }


  bool pixel_complete = false;
  switch(state)
  {

    case detect_sync:

      if(sync_found)
      {
        uint32_t least_error = UINT32_MAX;
        for(uint8_t mode = 0; mode < num_modes; ++mode)
        {
         
          if( line_length > (99*modes[mode].samples_per_line)/(100*m_scale) and line_length < (101*modes[mode].samples_per_line)/(100*m_scale) )
          {
            uint32_t error = abs((int32_t)(line_length)-(int32_t)(modes[mode].samples_per_line/m_scale));
            if(error < least_error)
            {
              decode_mode = (e_mode)mode;
              least_error = error;
              mean_samples_per_line = modes[mode].samples_per_line;
            }
            confirm_count = 0;
            state = confirm_sync;

          }
        }
      }

      break;

    case confirm_sync:

      if(sync_found)
      {
        if( line_length > (99*modes[decode_mode].samples_per_line)/(100*m_scale) and line_length < (101*modes[decode_mode].samples_per_line)/(100*m_scale) )
        {
          state = decode_line;
          confirmed_sync_sample = sample_number;
          pixel_accumulator = 0;
          pixel_n = 0;
          last_x = 0;
          image_sample = 0;
          sync_timeout = m_timeout;
        }
        else
        {
          confirm_count ++;
          if(confirm_count == 4)
          {
            state = detect_sync;
          }
        }
      }

      break;

    case decode_line:
    {

      uint16_t x, y;
      uint8_t colour;
      sample_to_pixel(x, y, colour, image_sample);

      if(x != last_x && colour < 4 && pixel_n)
      {
        //output pixel
        pixel_complete = true;
        pixel = pixel_accumulator/pixel_n;
        pixel_y = y;
        pixel_x = last_x;
        pixel_colour = colour;

        //reset accumulator for next pixel
        pixel_accumulator = 0;
        pixel_n = 0;
        last_x = x;
      }

      //end of image
      if(y == 256 || y == modes[decode_mode].max_height)
      {
        state = detect_sync;
        sync_counter = 0;
        break;
      }

      //Auto Slant Correction
      if(sync_found)
      {
        //confirm sync if close to expected time
        if( line_length > (99*modes[decode_mode].samples_per_line)/(100*m_scale) && line_length < (101*modes[decode_mode].samples_per_line)/(100*m_scale) )
        {
          sync_timeout = m_timeout; //reset timeout on each good sync pulse
          const uint32_t samples_since_confirmed = sample_number-confirmed_sync_sample;
          const uint16_t num_lines = round(1.0*m_scale*samples_since_confirmed/modes[decode_mode].samples_per_line);
          if(m_auto_slant_correction)
          {
            mean_samples_per_line = mean_samples_per_line - (mean_samples_per_line >> 2) + ((m_scale*samples_since_confirmed/num_lines) >> 2);
          }
        }
      }

      //if no hsync seen, go back to idle
      else
      {
        sync_timeout--;
        if(!sync_timeout)
        {
          state = detect_sync;
          sync_counter = 0;
          break;
        }
      }

      //colour pixels
      pixel_accumulator += frequency_to_brightness(sample);
      pixel_n++;
      image_sample+=m_scale;

      break;

    }
  }

  sample_number++;
  last_sample = sample;
  debug_state = state;
  return pixel_complete;

}

e_mode c_sstv_decoder :: get_mode()
{
  return decode_mode;  
}

s_sstv_mode * c_sstv_decoder :: get_modes()
{
  return &modes[0];
}
void c_sstv_decoder :: set_timeout_seconds(uint8_t timeout)
{
  m_timeout = timeout * m_Fs;
}
void c_sstv_decoder :: set_auto_slant_correction(bool enable)
{
  m_auto_slant_correction = enable;
}
