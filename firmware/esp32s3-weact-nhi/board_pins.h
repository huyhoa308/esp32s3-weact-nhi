// Single source of truth for every GPIO used (or reserved) by the
// "Nhi" desk robot. Two hardware variants share the same firmware:
//
//   NHI_HW_OLED = 1 (default): red xiaozhi "bread-compact" PCB, ESP32-S3 N16R8
//     clone, SSD1306 128x64 OLED (I2C), INMP441 mic + MAX98357 amp on two
//     separate I2S buses. Pins follow the silkscreen of that PCB (same as the
//     upstream bread-compact-wifi board).
//   NHI_HW_OLED = 0: WeAct CoreBoard + ILI9341 TFT + INMP441 / PCM5100A sharing
//     one I2S bus (pins verified on the dog robot bench, HARDWARE_SETUP.md).
#pragma once

#include <driver/gpio.h>

#ifndef NHI_HW_OLED
#define NHI_HW_OLED 1
#endif

#if NHI_HW_OLED
// ---- OLED SSD1306 128x64 (I2C, header "OLED-1.3": VCC GND SCL SDA) ----------
#define PIN_OLED_SDA  GPIO_NUM_41
#define PIN_OLED_SCL  GPIO_NUM_42

// ---- Audio: simplex, mic and speaker on their own clocks --------------------
#define PIN_MIC_WS    GPIO_NUM_4   // INMP441 WS
#define PIN_MIC_SCK   GPIO_NUM_5   // INMP441 SCK
#define PIN_MIC_SD    GPIO_NUM_6   // INMP441 SD -> ESP32
#define PIN_SPK_DIN   GPIO_NUM_7   // ESP32 -> MAX98357 DIN
#define PIN_SPK_BCLK  GPIO_NUM_15  // MAX98357 BCLK
#define PIN_SPK_LRC   GPIO_NUM_16  // MAX98357 LRC

// ---- Head servos (signal only; V+ from a separate 5 V >= 2 A supply) --------
// Free pins on the left DevKit header; GPIO 4/5 are taken by the mic here.
#define PIN_SERVO_PAN   GPIO_NUM_17  // turn left / right
#define PIN_SERVO_TILT  GPIO_NUM_18  // nod down / up

#else
// ---- TFT ILI9341 2.4"/2.8" (SPI) -------------------------------------------
#define PIN_TFT_DC    GPIO_NUM_38
#define PIN_TFT_CS    GPIO_NUM_39
#define PIN_TFT_SCLK  GPIO_NUM_40
#define PIN_TFT_MOSI  GPIO_NUM_41
#define PIN_TFT_MISO  GPIO_NUM_NC  // write-only panel
#define PIN_TFT_RST   GPIO_NUM_47
#define PIN_TFT_BL    GPIO_NUM_NC  // backlight tied to 3.3V

// ---- Audio: one full-duplex I2S bus -----------------------------------------
// INMP441 (mic, L/R -> GND = left slot) and the WeAct I2S Speaker V1
// (PCM5100A: VCC 5V, SD 3.3V, MC GND) share BCK and WS.
#define PIN_I2S_BCLK  GPIO_NUM_16
#define PIN_I2S_WS    GPIO_NUM_17
#define PIN_I2S_DIN   GPIO_NUM_14  // INMP441 SD  -> ESP32
#define PIN_I2S_DOUT  GPIO_NUM_15  // ESP32 -> DAC DIN

// ---- Head servos (signal only; V+ from a separate 5 V >= 2 A supply) --------
// Both must be 180 deg servos. The old FR leg on GPIO 4 was a 360 deg servo.
#define PIN_SERVO_PAN   GPIO_NUM_4  // turn left / right
#define PIN_SERVO_TILT  GPIO_NUM_5  // nod down / up
#endif

// ---- UI (both variants) ----------------------------------------------------
#define PIN_BUTTON_TALK GPIO_NUM_0   // BOOT button, input only
#define PIN_STATUS_LED  GPIO_NUM_48  // onboard WS2812

// ---- Reserved (do not use) -------------------------------------------------
// USB D- 19 / D+ 20               : native USB
// Never use: 0 as output, 3/45/46 (strapping), 26..32 (flash), 35..37 (OPI PSRAM)
