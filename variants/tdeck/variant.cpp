#include "esp32-hal-gpio.h"
#include "pins_arduino.h"

extern "C"
{

    // Run before setup() so the previous frame is hidden as early as possible.
    void initVariant(void)
    {
#ifdef DISP_BL
        if (DISP_BL >= 0)
        {
            pinMode(DISP_BL, OUTPUT);
            digitalWrite(DISP_BL, LOW);
        }
#endif

#ifdef BOARD_POWERON
        pinMode(BOARD_POWERON, OUTPUT);
        digitalWrite(BOARD_POWERON, HIGH);
#endif

#ifdef SD_CS
        pinMode(SD_CS, OUTPUT);
        digitalWrite(SD_CS, HIGH);
#endif

#ifdef LORA_CS
        pinMode(LORA_CS, OUTPUT);
        digitalWrite(LORA_CS, HIGH);
#endif

#ifdef DISP_CS
        pinMode(DISP_CS, OUTPUT);
        digitalWrite(DISP_CS, HIGH);
#endif
    }
}
