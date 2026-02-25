<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-LoRa-Pager🌟</h1>


## `1` Overview

* This page introduces the hardware parameters related to `LilyGO T-LoRa-Pager`

```bash

/---------------------------------------------------\
| ┌───────────────────────────────────────────┐ |-| |
| |                                           | |/| |
| |               480 x 222 IPS               | |/| |  
| |                                           | |/| |
| └───────────────────────────────────────────┘ |-| |
|                                                   |
|                                                   |
|                                                   |
|                                                   |
\---|RST|--|BOOT|--|POWER|--|SD SOCKET|--|USB-C|----/
      ^       ^       ^          ^           ^
      |       |       |          |           |
      |       |       |          |           └─── The adapter is used as a charging and 
      |       |       |          |                programming interface, and the USB-C can 
      |       |       |          |                be programmed to power external devices
      |       |       |          |      
      |       |       |          └────── Supports up to 32 GB SD memory card
      |       |       |               
      |       |       └───────────── The power button is only valid when the device is 
      |       |                     turned off and cannot be customized or program controlled.
      |       |
      |       └───────────────────── (GPIO0) Custom Button or Enter download Mode
      |
      └───────────────────────────── Click to reset the device, 
                                     it cannot be programmed or controlled by the program






```

### Extension interface

```bash

>----------Place the screen facing up---------------<
|---------------------------------------------------|
|     | SCL | SDA | MISO  | SCK  | TX | GND  |      |
|     | 5V  | CE  | GPIO9 | MOSI | RX | 3.3V |      |
|---------------------------------------------------|

* CE is XL9555 GPIO9
* TX is ESP32-S3 GPIO43
* RX is ESP32-S3 GPIO44
* MISO is ESP32-S3 GPIO33
* MOSI is ESP32-S3 GPIO34
* SCK is ESP32-S3 GPIO35
* SDA is ESP32-S3 GPIO3
* SCL is ESP32-S3 GPIO2

```

### nRF24L01 PA Shield interface

```bash
>----------Place the screen facing up---------------<
|---------------------------------------------------|
|     | SCL | SDA | MISO  | SCK  | TX | GND  |      |
|     | 5V  | CE  | GPIO9 | MOSI | RX | 3.3V |      |
|---------------------------------------------------|

* CE is XL9555 GPIO9 , nRF24L01 Shield Tx/Rx Control, LOW:Rx HIGH:Tx
* TX is ESP32-S3 GPIO43, nRF24L01 Shield CE Pin
* RX is ESP32-S3 GPIO44, nRF24L01 Shield CS Pin
* MISO is ESP32-S3 GPIO33, nRF24L01 Shield MISO Pin
* MOSI is ESP32-S3 GPIO34, nRF24L01 Shield MOSI Pin
* SCK is ESP32-S3 GPIO35, nRF24L01 Shield SCK Pin
* SDA is ESP32-S3 GPIO3, nRF24L01 Shield No Connect
* SCL is ESP32-S3 GPIO2, nRF24L01 Shield No Connect

```

### ✨ Hardware-Features

| Features                         | Params                           |
| -------------------------------- | -------------------------------- |
| SOC                              | [Espressif ESP32-S3][1]          |
| Flash                            | 16MB(QSPI)                       |
| PSRAM                            | 8MB (QSPI)                       |
| GNSS                             | [UBlox MIA-M10Q][2]              |
| LoRa                             | [Semtech SX1262][3]              |
| NFC                              | [ST25R3916][4]                   |
| Smart sensor                     | [Bosch BHI260AP][5]              |
| Real-Time Clock                  | [NXP PCF85063A][6]               |
| Battery Charger                  | [Ti BQ25896][7]                  |
| Battery Gauge                    | [Ti BQ27220][8]                  |
| Haptic driver                    | [Ti DRV2605][9]                  |
| Audio Codec                      | [Everest-semi ES8311][10]        |
| GPIO Expand                      | [XINLUDA XL9555][11]             |
| I2C Keyboard                     | [Ti TCA8418][12]                 |
| Audio Power Amplifier            | [Nsiway NS4150B(3W Class D)][13] |
| Display Backlight Driver         | [AW9364 16-Level Led Driver][14] |
| SD Card Socket                   | ✅️ Maximum 32GB (FAT32 format)    |
| External low speed clock crystal | ✅️                                |

> \[!TIP]
> 
> * SD card only supports FAT format, please pay attention to the selection of SD format
> * Device shutdown can only shut down the device when no USB is connected.
> * The PWR button can only be used to wake up the device by pressing it for one second when the device is turned off. It cannot be used for programming.
> * ST25R3916 (NFC) does not have an integrated capacitive sensor, which means that to read a card, the reader must be turned on, and the presence of a card cannot be detected by turning on the capacitive sensor.
> * ESP32-S3 uses an external QSPI Flash and PSRAM solution, not a built-in PSRAM or Flash solution
> * USB/charging state is detected via the BQ25896 PMU (VBUS/charge status), so UI charging indicators and software shutdown checks rely on PMU detection.

[1]: https://www.espressif.com.cn/en/products/socs/esp32-s3 "ESP32-S3"
[2]: https://www.u-blox.com/en/product/mia-m10-series "UBlox MIA-M10Q"
[3]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 "Semtech SX1262"
[4]: https://www.st.com/en/nfc/st25r3916.html "ST25R3916"
[5]: https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ab "BHI260AP"
[6]: https://www.nxp.com/products/PCF85063A "PCF85063A"
[7]: https://www.ti.com/product/BQ25896 "BQ25896"
[8]: https://www.ti.com/product/BQ27220 "BQ27220"
[9]: https://www.ti.com/product/DRV2605 "DRV2605"
[10]: http://www.everest-semi.com/pdf/ES8311%20PB.pdf "ES8311"
[11]: https://www.xinluda.com/en/I2C-to-GPIO-extension/ "XL9555"
[12]: https://www.ti.com/product/TCA8418 "TCA8418"
[13]: http://www.nsiway.com.cn/product/58.html "NS4150B"
[14]: https://item.szlcsc.com/datasheet/AW9364DNR/385721.html "AW9364"

### ✨ Display-Features

| Features              | Params        |
| --------------------- | ------------- |
| Resolution            | 480 x 222     |
| Display Size          | 2.33 Inch     |
| Luminance on surface  | 450 cd/m²     |
| Driver IC             | ST7796U (SPI) |
| Contrast ratio        | 1000:1        |
| Color gamut           | 70%           |
| PPI                   | 221           |
| Display Colors        | 262K          |
| View Direction        | All  (IPS)    |
| Operating Temperature | -20～70°C     |

### 📍 [Pins Map](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_tlora_pager/pins_arduino.h)

| Name                                 | GPIO NUM                       | Free |
| ------------------------------------ | ------------------------------ | ---- |
| Custom Pin                           | GPIO9 (External 12-Pin socket) | ✅️    |
| Uart1 TX                             | 43(External 12-Pin socket)     | ✅️    |
| Uart1 RX                             | 44(External 12-Pin socket)     | ✅️    |
| SDA                                  | 3                              | ❌    |
| SCL                                  | 2                              | ❌    |
| SPI MOSI                             | 34                             | ❌    |
| SPI MISO                             | 33                             | ❌    |
| SPI SCK                              | 35                             | ❌    |
| SD CS                                | 21                             | ❌    |
| SD MOSI                              | Share with SPI bus             | ❌    |
| SD MISO                              | Share with SPI bus             | ❌    |
| SD SCK                               | Share with SPI bus             | ❌    |
| Keyboard(**TCA8418**) SDA            | Share with I2C bus             | ❌    |
| Keyboard(**TCA8418**) SCL            | Share with I2C bus             | ❌    |
| Keyboard(**TCA8418**) Interrupt      | 6                              | ❌    |
| Keyboard Backlight                   | 46                             | ❌    |
| Rotary Encoder A                     | 40                             | ❌    |
| Rotary Encoder B                     | 41                             | ❌    |
| Rotary Encoder Center                | 7                              | ❌    |
| RTC(**PCF85063A**) SDA               | Share with I2C bus             | ❌    |
| RTC(**PCF85063A**) SCL               | Share with I2C bus             | ❌    |
| RTC(**PCF85063A**) Interrupt         | 1                              | ❌    |
| NFC(**ST25R3916**) CS                | 39                             | ❌    |
| NFC(**ST25R3916**) Interrupt         | 5                              | ❌    |
| NFC(**ST25R3916**) MOSI              | Share with SPI bus             | ❌    |
| NFC(**ST25R3916**) MISO              | Share with SPI bus             | ❌    |
| NFC(**ST25R3916**) SCK               | Share with SPI bus             | ❌    |
| Sensor(**BHI260**) Interrupt         | 8                              | ❌    |
| Sensor(**BHI260**) SDA               | Share with I2C bus             | ❌    |
| Sensor(**BHI260**) SCL               | Share with I2C bus             | ❌    |
| Audio Codec(**ES8311**) WS           | 18                             | ❌    |
| Audio Codec(**ES8311**) SCK          | 11                             | ❌    |
| Audio Codec(**ES8311**) MCLK         | 10                             | ❌    |
| Audio Codec(**ES8311**) data out     | 45                             | ❌    |
| Audio Codec(**ES8311**) data in      | 17                             | ❌    |
| Audio Codec(**ES8311**) SDA          | Share with I2C bus             | ❌    |
| Audio Codec(**ES8311**) SCL          | Share with I2C bus             | ❌    |
| GNSS(**MIA-M10Q**) TX                | 12                             | ❌    |
| GNSS(**MIA-M10Q**) RX                | 4                              | ❌    |
| GNSS(**MIA-M10Q**) PPS               | 13                             | ❌    |
| LoRa(**SX1262 or SX1280**) SCK       | Share with SPI bus             | ❌    |
| LoRa(**SX1262 or SX1280**) MISO      | Share with SPI bus             | ❌    |
| LoRa(**SX1262 or SX1280**) MOSI      | Share with SPI bus             | ❌    |
| LoRa(**SX1262 or SX1280**) RESET     | 47                             | ❌    |
| LoRa(**SX1262 or SX1280**) BUSY      | 48                             | ❌    |
| LoRa(**SX1262 or SX1280**) CS        | 36                             | ❌    |
| LoRa(**SX1262 or SX1280**) Interrupt | 14                             | ❌    |
| Display CS                           | 38                             | ❌    |
| Display MOSI                         | Share with SPI bus             | ❌    |
| Display MISO                         | Share with SPI bus             | ❌    |
| Display SCK                          | Share with SPI bus             | ❌    |
| Display DC                           | 37                             | ❌    |
| Display RESET                        | Not Connected                  | ❌    |
| Display Backlight(16 Level)          | 42                             | ❌    |
| Gauge(**BQ27220**) SDA               | Share with I2C bus             | ❌    |
| Gauge(**BQ27220**) SCL               | Share with I2C bus             | ❌    |
| Charger(**BQ25896**) SDA             | Share with I2C bus             | ❌    |
| Charger(**BQ25896**) SCL             | Share with I2C bus             | ❌    |
| Haptic Driver(**DRV2605**) SDA       | Share with I2C bus             | ❌    |
| Haptic Driver(**DRV2605**) SCL       | Share with I2C bus             | ❌    |
| Expand(**XL9555**) SDA               | Share with I2C bus             | ❌    |
| Expand(**XL9555**) SCL               | Share with I2C bus             | ❌    |
| Expand(**XL9555**) GPIO0             | Haptic Driver Enable           | ❌    |
| Expand(**XL9555**) GPIO1             | Audio Power Amplifier Enable   | ❌    |
| Expand(**XL9555**) GPIO2             | Keyboard RESET                 | ❌    |
| Expand(**XL9555**) GPIO3             | LoRa Power supply Enable       | ❌    |
| Expand(**XL9555**) GPIO4             | GNSS Power supply Enable       | ❌    |
| Expand(**XL9555**) GPIO5             | NFC Power supply Enable        | ❌    |
| Expand(**XL9555**) GPIO6             | ~~Display RESET~~ (No connect) | ❌    |
| Expand(**XL9555**) GPIO7             | GNSS RESET                     | ❌    |
| Expand(**XL9555**) GPIO10            | Keyboard Power supply Enable   | ❌    |
| Expand(**XL9555**) GPIO11            | External 12-Pin socket         | ✅️    |
| Expand(**XL9555**) GPIO12            | SD Insert Detect               | ❌    |
| Expand(**XL9555**) GPIO14            | SD Power supply Enable         | ❌    |
<!-- | Expand(**XL9555**) GPIO13            | SD PullUp Enable               | ❌    | -->

### 🧑🏼‍🔧 I2C Devices Address

| Devices                        | 7-Bit Address | Share Bus |
| ------------------------------ | ------------- | --------- |
| [Codec ES8311][10]             | 0x18          | ✅️         |
| [Expands IO XL9555][11]        | 0x20          | ✅️         |
| [Smart sensor BHI260AP][5]     | 0x28          | ✅️         |
| [Real-Time Clock PCF85063A][6] | 0x51          | ✅️         |
| [PowerManage BQ25896][7]       | 0x6B          | ✅️         |
| [Gauge BQ27220][8]             | 0x55          | ✅️         |
| [Keyboard TCA8418][12]         | 0x34          | ✅️         |
| [Haptic driver DRV2605][9]     | 0x5A          | ✅️         |

### ⚡ PowerManage Channel

| Channel                  | Peripherals        |
| ------------------------ | ------------------ |
| Expand(**XL9555**) GPIO0 | **DRV2605 Enable** |
| Expand(**XL9555**) GPIO1 | **Speaker**        |
| Expand(**XL9555**) GPIO3 | **LoRa**           |
| Expand(**XL9555**) GPIO4 | **GNSS**           |
| Expand(**XL9555**) GPIO5 | **NFC**            |
| Expand(**XL9555**) GPIO8 | **Keyboard**       |
| Expand(**XL9555**) GPIO14 | **SD Card**       |

### ⚡ Electrical parameters

| Features                   | Details                    |
| -------------------------- | -------------------------- |
| 🔗USB-C Input Voltage       | 3.9V-6V                    |
| 🔗USB-C Output Voltage      | 4.55-5.55V                 |
| ⚡USB-C Output Current      | 0.5-1A                     |
| ⚡Charge Current            | 0-3008mA(\(Programmable\)) |
| 🔋Battery Voltage           | 3.7V                       |
| 🔋Battery capacity          | 1500mA (\(5.55Wh\))        |
| 🔋Charge Temperature  Range | 0~60°                      |

> \[!IMPORTANT]
> ⚠️ Recommended to use a charging current lower than 750mA.
> The charging current should not be greater than half of the battery capacity

### ⚡ Power consumption reference

| Mode       | Wake-Up Mode | Current |
| ---------- | ------------ | ------- |
| DeepSleep  | BootButton   | 530uA   |
| DeepSleep  | Timer        | 530uA   |
| LightSleep | BootButton   | ~2.26mA |
| Power OFF  | PowerButton  | 26uA    |

### Resource

* [Radio-SX1262(Sub 1G LoRa and FSK )](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262)
* [Radio-SX1280(2.4G LoRa,FLRC,(G)FSK)](https://www.semtech.cn/products/wireless-rf/lora-connect/sx1280)
* [Radio-CC1101(Sub 1G (G)MSK, 2(G)FSK, 4(G)FSK, ASK, OOK)](https://www.ti.com/product/CC1101)
* [Radio-LR1121(Sub 1G + 2.4G LoRa)](https://www.semtech.com/products/wireless-rf/lora-connect/lr1121)
* [Radio-SI4432(Sub 1G ISM)](https://www.silabs.com/wireless/proprietary/ezradiopro-sub-ghz-ics/device.si4432?tab=specs)
* [Schematic](../../schematic/T-Watch%20Ultra%20V1.0%20SCH%2025-07-24.pdf)
