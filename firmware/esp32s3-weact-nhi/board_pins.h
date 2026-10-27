// Single source of truth for every GPIO used (or reserved) by the
// "Nhi" desk robot on a WeAct ESP32-S3 CoreBoard N16R8.
// Display and audio pins are the ones verified on the bench for the dog robot
// (esp32s3_weact_xiaozhirobot, HARDWARE_SETUP.md). The two head servos reuse
// the old FR / RR leg pins (GPIO 4 / 5).
#pragma once

#include <driver/gpio.h>

// ---- TFT ILI9341 2.4"/2.8" (SPI) -------------------------------------------
#define PIN_TFT_DC    GPIO_NUM_38
#define PIN_TFT_CS    GPIO_NUM_39
#define PIN_TFT_SCLK  GPIO_NUM_40
#define PIN_TFT_MOSI  GPIO_NUM_41
#define PIN_TFT_MISO  GPIO_NUM_NC  // write-only panel
#define PIN_TFT_RST   GPIO_NUM_47
#define PIN_TFT_BL    GPIO_NUM_NC  // backlight tied to 3.3V

// ---- Audio: one full-duplex I2S bus -----------------------------------------
// INMP441 (mic, L/R -> GND = left slot) and the speaker DAC/amp share BCK and WS.
// Works with the WeAct I2S Speaker V1 (PCM5100A: VCC 5V, SD 3.3V, MC GND) and
// with a MAX98357A (BCLK / LRC / DIN on the same pins, GAIN + SD open).
// If your red PCB routes audio to BCLK 11 / WS 12 / DIN 14 / DOUT 13, change
// only these four lines.
#define PIN_I2S_BCLK  GPIO_NUM_16
#define PIN_I2S_WS    GPIO_NUM_17
#define PIN_I2S_DIN   GPIO_NUM_14  // INMP441 SD  -> ESP32
#define PIN_I2S_DOUT  GPIO_NUM_15  // ESP32 -> DAC/amp DIN

// ---- Head servos (signal only; V+ from a separate 5 V >= 2 A supply) --------
// Both must be 180 deg servos (SG90 / MG90S). The old FR leg on GPIO 4 was a
// 360 deg servo: replace it before using it as PAN.
#define PIN_SERVO_PAN   GPIO_NUM_4  // turn left / right
#define PIN_SERVO_TILT  GPIO_NUM_5  // nod down / up

// ---- UI --------------------------------------------------------------------
#define PIN_BUTTON_TALK GPIO_NUM_0   // BOOT button, input only
#define PIN_STATUS_LED  GPIO_NUM_48  // onboard WS2812

// ---- Free / reserved (do not use) ------------------------------------------
// GPIO 6, 7 (old RL / FL legs), 8, 9, 21 (old I2C ToF) : free
// USB D- 19 / D+ 20               : native USB
// Never use: 0 as output, 3/45/46 (strapping), 26..32 (flash), 35..37 (OPI PSRAM)
