// HeadServos: PAN (left/right) + TILT (down/up) head with safety rules:
//  - every angle clamped to the per-axis mechanical limits in kAxisConfig
//  - smooth moves: acceleration ramp at the start, ease-out at the end
//  - PAN PWM detached after a pose is held SERVO_DETACH_AFTER_MS (quiet)
//  - runs in its own task; Play() / Glance() never block the caller
#pragma once

#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>

#include "nhi_config.h"

enum class HeadMotion : uint8_t {
    kNone,
    kCenter,      // look straight ahead
    kHappyShake,  // playful left/right shake
    kSadDroop,    // slow head down, held
    kAngryShake,  // short fast "no" shake
    kLookUp,      // surprised: head up
    kTiltSide,    // thinking: turned to one side, slightly up
    kListenUp,    // listening: slightly up, centred
    kNod,         // speaking: small nod (keeps the pan)
    kSway,        // speaking: slow small sway (keeps the tilt)
    kSleepDroop,  // sleepy: slowly down, then still
    kRelax,       // stop PWM on both servos now
};

const char* HeadMotionName(HeadMotion m);

class HeadServos {
public:
    HeadServos();
    // Creates the task. Servos stay unpowered until AttachSequential().
    void Begin();
    // Attach both servos at neutral, one after the other (call once Wi-Fi + audio are up).
    void AttachSequential();
    // Replace whatever is playing with this motion (non-blocking).
    void Play(HeadMotion motion);
    // Look around: offsets from neutral in logical degrees (PAN > 0 = robot's left,
    // TILT > 0 = up). Replaces whatever is playing.
    void Glance(int pan_offset, int tilt_offset);
    bool attached() const { return initialized_; }
    bool busy() const { return busy_; }

private:
    struct Keyframe {
        uint8_t angle[AXIS_COUNT];  // logical degrees; 0 = keep current
        uint16_t speed;             // deg/s
        uint16_t hold_ms;
    };
    struct Axis {
        float current = SERVO_NEUTRAL_DEG;
        float target = SERVO_NEUTRAL_DEG;
        float velocity = 0;
        float speed = SERVO_SPEED_NORMAL;
        bool attached = false;
        int64_t settled_ms = 0;
        ledc_channel_t channel = LEDC_CHANNEL_0;
    };
    enum Cmd : uint8_t { kCmdMotion, kCmdGlance, kCmdAttach };
    struct Command {
        Cmd cmd;
        HeadMotion motion;
        int8_t pan;
        int8_t tilt;
    };

    static void TaskEntry(void* arg);
    void Loop();
    void DoAttach();
    void LoadMotion(HeadMotion m);
    void LoadGlance(int pan_offset, int tilt_offset);
    void WriteAngle(int axis, float logical_deg);
    void Detach(int axis);
    static float Clamp(int axis, float logical_deg);
    static int64_t NowMs();

    Axis axes_[AXIS_COUNT];
    const Keyframe* frames_ = nullptr;
    int frame_count_ = 0;
    int frame_index_ = 0;
    bool frame_active_ = false;
    int64_t frame_settled_ms_ = 0;
    Keyframe glance_[1] = {};
    volatile bool initialized_ = false;
    volatile bool busy_ = false;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
};
