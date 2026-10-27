// Tunable constants for the eyes, the head servos and the behaviour.
// No magic numbers elsewhere.
#pragma once

#include <cstdint>

#include "board_pins.h"

// ============================================================================
// Eyes
// ============================================================================
// Colours are plain RGB565 (0xRRRR RGGG GGGB BBBB). The canvas swaps bytes itself.
#define EYES_COLOR_MAIN       0x07FF  // cyan (default look)
#define EYES_COLOR_BG         0x0000  // black
#define EYES_COLOR_TEXT       0xFFFF  // subtitle text (notifications, activation code)

// Per-mood eye colours. The eye colour fades smoothly between them.
#define EYES_COLOR_HAPPY      0xFFE0  // yellow
#define EYES_COLOR_LOVE       0xFB56  // pink
#define EYES_COLOR_SAD        0x3B9F  // soft blue
#define EYES_COLOR_ANGRY      0xF800  // red
#define EYES_COLOR_SURPRISED  0xFFFF  // white
#define EYES_COLOR_CONFUSED   0xB41F  // violet
#define EYES_COLOR_SLEEPY     0x6D7B  // dim blue-grey
#define EYES_COLOR_COOL       0x07E0  // green
#define EYES_COLOR_SILLY      0xFD20  // orange
#define EYES_COLOR_LISTEN     0x07FF  // cyan
#define EYES_COLOR_THINK      0x867F  // light blue
#define EYES_COLOR_ERROR      0xFD20  // orange

// Overlay colours (hearts, tears, blush, symbols)
#define FX_COLOR_HEART        0xF8B2  // red-pink
#define FX_COLOR_TEAR         0x5D9F  // light blue
#define FX_COLOR_BLUSH        0xF9F2  // pink
#define FX_COLOR_ANGER        0xF800  // red
#define FX_COLOR_ZZZ          0xAD7F  // pale blue
#define FX_COLOR_SYMBOL       0xFFFF  // "?" "!" text
#define FX_COLOR_SPARKLE      0xFFF0  // pale yellow
#define FX_COLOR_GLASSES      0x2104  // almost black (sunglasses)
#define FX_COLOR_NOTE         0xFFE0  // music note

#define EYES_COLOR_FADE_STEP  24      // 0..256 per frame (24 ~ 10 frames to fade)

#define EYES_FPS              30      // target frame rate (25..30)
#define EYES_TASK_PRIORITY    2       // low: must never starve audio
#define EYES_TASK_STACK       4096
#define EYES_TASK_CORE        1       // tskNO_AFFINITY is also fine

#define EYES_WIDTH            90
#define EYES_HEIGHT           80
#define EYES_RADIUS           20
#define EYES_SPACE            40
// "Wide open" variant used for listening / surprised
#define EYES_WIDE_WIDTH       96
#define EYES_WIDE_HEIGHT      104
// "Squint" variant used for cool / skeptical
#define EYES_SQUINT_HEIGHT    40

#define EYES_SWEAT_SCALE      3.0f    // sweat drops were sized for 128x64

// Push buffer: internal DMA RAM, two ping-pong chunks of N lines each
#define EYES_PUSH_LINES       24

// Bottom subtitle strip (ASCII + Vietnamese 8x16 font, scaled)
#define EYES_TEXT_SCALE       2
#define EYES_TEXT_MAX_LINES   2
// How long an assistant sentence stays on screen
#define EYES_CHAT_TEXT_MS     6000

// ============================================================================
// Head servos (PAN = left/right, TILT = down/up)
// ============================================================================
// Master switch: 0 = servos off (no PWM task, signal pins held LOW, head limp).
#define NHI_SERVOS_ENABLED       1

enum HeadAxis : uint8_t { AXIS_PAN = 0, AXIS_TILT = 1, AXIS_COUNT = 2 };

struct AxisConfig {
    gpio_num_t gpio;
    int8_t trim_deg;   // added after the mirror, to centre the head mechanically
    bool invert;       // mirror around 90 deg (servo mounted the other way)
    uint8_t min_deg;   // mechanical limits, logical degrees (never exceeded)
    uint8_t max_deg;
    bool detach_idle;  // cut PWM when holding still (quiet, no jitter)
};

// Logical convention: 90 = looking straight ahead.
//   PAN  > 90 = turn to the robot's LEFT (viewer's right), < 90 = robot's right.
//   TILT > 90 = look UP,                                   < 90 = look down.
// Fix a wrong direction with `invert`, an off-centre head with `trim_deg`.
// TILT keeps its PWM by default so the screen does not sag under its weight.
static constexpr AxisConfig kAxisConfig[AXIS_COUNT] = {
    {PIN_SERVO_PAN, 0, false, 50, 130, true},
    {PIN_SERVO_TILT, 0, false, 70, 115, false},
};

#define SERVO_NEUTRAL_DEG        90
#define SERVO_PULSE_MIN_US       500   // pulse at 0 deg (as test_servo.cpp)
#define SERVO_PULSE_MAX_US       2500  // pulse at 180 deg
#define SERVO_PWM_HZ             50
#define SERVO_PWM_BITS           14
#define SERVO_LEDC_TIMER         LEDC_TIMER_1
#define SERVO_LEDC_FIRST_CHANNEL LEDC_CHANNEL_2  // channels 2..3

#define SERVO_SPEED_SLOW         40    // deg/s: sad, sleepy
#define SERVO_SPEED_NORMAL       90    // deg/s: most moves
#define SERVO_SPEED_FAST         180   // deg/s: shakes
#define SERVO_ACCEL_DEG_S2       720   // ramp-up (smooth start)
#define SERVO_EASE_DEG           12    // slow down over the last N degrees (smooth stop)
#define SERVO_DETACH_AFTER_MS    1500  // PAN: stop PWM when a pose is held this long
#define SERVO_TICK_MS            20    // control loop period
#define SERVO_INIT_GAP_MS        400   // sequential attach at boot
#define SERVO_TASK_PRIORITY      3
#define SERVO_TASK_STACK         4096

// ============================================================================
// Behaviour
// ============================================================================
#define NHI_SPEAK_NOD_EVERY_MS   3000   // nod / sway cadence while speaking
#define NHI_SURPRISE_MS          1500   // how long "surprised" curiosity lasts
#define NHI_EMOTION_HOLD_MS      8000   // an emotion fades back to neutral after this (idle)
#define NHI_IDLE_SLEEPY_AFTER_MS 180000 // idle this long -> sleepy eyes + Zzz + head down
#define NHI_IDLE_FIDGET_MIN_MS   10000  // random idle look-around interval
#define NHI_IDLE_FIDGET_MAX_MS   22000
#define NHI_IDLE_HEAD_EVERY_N    2      // only 1 idle fidget in N also moves the head
#define NHI_GLANCE_PAN_DEG       18     // idle glance: head turn that follows the eyes
#define NHI_GLANCE_TILT_DEG      8
#define NHI_POLL_MS              50     // state polling period
