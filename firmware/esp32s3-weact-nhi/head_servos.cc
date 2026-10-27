#include "head_servos.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cmath>

#define TAG "HeadServos"

// ---------------------------------------------------------------------------
// Motion library (logical degrees, 90 = straight ahead; 0 = keep this axis).
// PAN > 90 = robot's left, TILT > 90 = up. Every angle is clamped again to
// kAxisConfig limits, so a table typo can never hit the mechanical stops.
// ---------------------------------------------------------------------------
namespace {
struct Motion {
    const void* frames;
    int count;
};
#define KF(pan, tilt, speed, hold) {{pan, tilt}, speed, hold}

struct KeyframeDef {
    uint8_t angle[AXIS_COUNT];
    uint16_t speed;
    uint16_t hold_ms;
};

constexpr uint16_t kSlow = SERVO_SPEED_SLOW;
constexpr uint16_t kNormal = SERVO_SPEED_NORMAL;
constexpr uint16_t kFast = SERVO_SPEED_FAST;

const KeyframeDef kCenter[] = {KF(90, 90, kNormal, 0)};
const KeyframeDef kHappyShake[] = {
    KF(90, 96, kNormal, 0), KF(108, 96, kFast, 40), KF(72, 96, kFast, 40),
    KF(108, 96, kFast, 40), KF(72, 96, kFast, 40),  KF(90, 94, kNormal, 0),
};
const KeyframeDef kSadDroop[] = {KF(90, 76, kSlow, 0)};
const KeyframeDef kAngryShake[] = {
    KF(100, 88, kFast, 0), KF(80, 88, kFast, 0), KF(100, 88, kFast, 0),
    KF(80, 88, kFast, 0),  KF(90, 88, kFast, 0),
};
const KeyframeDef kLookUp[] = {KF(90, 110, kFast, 0)};
// Same side as the "thinking" eyes (looking up-left on screen = robot's right).
const KeyframeDef kTiltSide[] = {KF(72, 98, kNormal, 0)};
const KeyframeDef kListenUp[] = {KF(90, 98, kNormal, 0)};
const KeyframeDef kNod[] = {
    KF(0, 84, kNormal, 60), KF(0, 95, kNormal, 60), KF(0, 90, kNormal, 0),
};
const KeyframeDef kSway[] = {
    KF(84, 0, kSlow, 100), KF(96, 0, kSlow, 100), KF(90, 0, kSlow, 0),
};
const KeyframeDef kSleepDroop[] = {KF(90, 80, kSlow, 0)};
#undef KF
#define MOTION(arr) Motion{arr, (int)(sizeof(arr) / sizeof(arr[0]))}

Motion MotionFor(HeadMotion m) {
    switch (m) {
        case HeadMotion::kCenter: return MOTION(kCenter);
        case HeadMotion::kHappyShake: return MOTION(kHappyShake);
        case HeadMotion::kSadDroop: return MOTION(kSadDroop);
        case HeadMotion::kAngryShake: return MOTION(kAngryShake);
        case HeadMotion::kLookUp: return MOTION(kLookUp);
        case HeadMotion::kTiltSide: return MOTION(kTiltSide);
        case HeadMotion::kListenUp: return MOTION(kListenUp);
        case HeadMotion::kNod: return MOTION(kNod);
        case HeadMotion::kSway: return MOTION(kSway);
        case HeadMotion::kSleepDroop: return MOTION(kSleepDroop);
        default: return Motion{nullptr, 0};
    }
}
}  // namespace

const char* HeadMotionName(HeadMotion m) {
    switch (m) {
        case HeadMotion::kNone: return "none";
        case HeadMotion::kCenter: return "center";
        case HeadMotion::kHappyShake: return "happy_shake";
        case HeadMotion::kSadDroop: return "sad_droop";
        case HeadMotion::kAngryShake: return "angry_shake";
        case HeadMotion::kLookUp: return "look_up";
        case HeadMotion::kTiltSide: return "tilt_side";
        case HeadMotion::kListenUp: return "listen_up";
        case HeadMotion::kNod: return "nod";
        case HeadMotion::kSway: return "sway";
        case HeadMotion::kSleepDroop: return "sleep_droop";
        case HeadMotion::kRelax: return "relax";
    }
    return "?";
}

HeadServos::HeadServos() {
    static_assert(sizeof(KeyframeDef) == sizeof(Keyframe), "keyframe layout");
    for (int i = 0; i < AXIS_COUNT; ++i) {
        axes_[i].channel = (ledc_channel_t)(SERVO_LEDC_FIRST_CHANNEL + i);
    }
}

int64_t HeadServos::NowMs() { return esp_timer_get_time() / 1000; }

float HeadServos::Clamp(int axis, float logical) {
    const AxisConfig& cfg = kAxisConfig[axis];
    return std::fmin(std::fmax(logical, (float)cfg.min_deg), (float)cfg.max_deg);
}

void HeadServos::Begin() {
    if (task_ != nullptr) return;
    queue_ = xQueueCreate(4, sizeof(Command));
    xTaskCreate(&HeadServos::TaskEntry, "head", SERVO_TASK_STACK, this, SERVO_TASK_PRIORITY, &task_);
}

void HeadServos::AttachSequential() {
    if (queue_ == nullptr) return;
    Command c = {kCmdAttach, HeadMotion::kNone, 0, 0};
    xQueueSend(queue_, &c, 0);
}

void HeadServos::Play(HeadMotion motion) {
    if (queue_ == nullptr || motion == HeadMotion::kNone) return;
    Command c = {kCmdMotion, motion, 0, 0};
    if (xQueueSend(queue_, &c, 0) != pdTRUE) {
        ESP_LOGW(TAG, "motion queue full, dropping %s", HeadMotionName(motion));
    }
}

void HeadServos::Glance(int pan_offset, int tilt_offset) {
    if (queue_ == nullptr) return;
    Command c = {kCmdGlance, HeadMotion::kNone, (int8_t)pan_offset, (int8_t)tilt_offset};
    xQueueSend(queue_, &c, 0);
}

void HeadServos::WriteAngle(int axis, float logical) {
    // Clamp the logical angle, apply mount direction + trim, keep inside 0..180.
    const AxisConfig& cfg = kAxisConfig[axis];
    logical = Clamp(axis, logical);
    float phys = cfg.invert ? 180.0f - logical : logical;
    phys += cfg.trim_deg;
    phys = std::fmin(std::fmax(phys, 0.0f), 180.0f);

    float pulse_us = SERVO_PULSE_MIN_US + (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) * phys / 180.0f;
    uint32_t period_us = 1000000 / SERVO_PWM_HZ;
    uint32_t duty = (uint32_t)(pulse_us * (float)(1 << SERVO_PWM_BITS) / (float)period_us);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, axes_[axis].channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, axes_[axis].channel);  // also re-enables output
    axes_[axis].attached = true;
}

void HeadServos::Detach(int axis) {
    ledc_stop(LEDC_LOW_SPEED_MODE, axes_[axis].channel, 0);
    axes_[axis].attached = false;
}

void HeadServos::DoAttach() {
    if (initialized_) return;
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = (ledc_timer_bit_t)SERVO_PWM_BITS;
    timer.timer_num = SERVO_LEDC_TIMER;
    timer.freq_hz = SERVO_PWM_HZ;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    for (int i = 0; i < AXIS_COUNT; ++i) {
        ledc_channel_config_t ch = {};
        ch.gpio_num = kAxisConfig[i].gpio;
        ch.speed_mode = LEDC_LOW_SPEED_MODE;
        ch.channel = axes_[i].channel;
        ch.timer_sel = SERVO_LEDC_TIMER;
        ch.duty = 0;
        ch.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&ch));
        // Position before first power-up is unknown, so this single jump is
        // unavoidable; one servo at a time keeps the inrush current low.
        axes_[i].current = axes_[i].target = SERVO_NEUTRAL_DEG;
        WriteAngle(i, SERVO_NEUTRAL_DEG);
        axes_[i].settled_ms = NowMs();
        ESP_LOGI(TAG, "attached %s on GPIO%d", i == AXIS_PAN ? "PAN" : "TILT",
                 (int)kAxisConfig[i].gpio);
        vTaskDelay(pdMS_TO_TICKS(SERVO_INIT_GAP_MS));
    }
    initialized_ = true;
}

void HeadServos::LoadMotion(HeadMotion m) {
    if (m == HeadMotion::kRelax) {
        frames_ = nullptr;
        frame_count_ = 0;
        for (int i = 0; i < AXIS_COUNT; ++i) {
            axes_[i].target = axes_[i].current;
            axes_[i].velocity = 0;
            if (axes_[i].attached) Detach(i);
        }
        busy_ = false;
        return;
    }
    Motion mo = MotionFor(m);
    if (mo.frames == nullptr) return;
    ESP_LOGI(TAG, "play %s", HeadMotionName(m));
    frames_ = static_cast<const Keyframe*>(mo.frames);
    frame_count_ = mo.count;
    frame_index_ = 0;
    frame_active_ = false;
    frame_settled_ms_ = 0;
    busy_ = true;
}

void HeadServos::LoadGlance(int pan_offset, int tilt_offset) {
    glance_[0].angle[AXIS_PAN] = (uint8_t)Clamp(AXIS_PAN, SERVO_NEUTRAL_DEG + pan_offset);
    glance_[0].angle[AXIS_TILT] = (uint8_t)Clamp(AXIS_TILT, SERVO_NEUTRAL_DEG + tilt_offset);
    glance_[0].speed = SERVO_SPEED_SLOW;
    glance_[0].hold_ms = 0;
    frames_ = glance_;
    frame_count_ = 1;
    frame_index_ = 0;
    frame_active_ = false;
    frame_settled_ms_ = 0;
    busy_ = true;
}

void HeadServos::TaskEntry(void* arg) { static_cast<HeadServos*>(arg)->Loop(); }

void HeadServos::Loop() {
    const float dt = SERVO_TICK_MS / 1000.0f;
    TickType_t last = xTaskGetTickCount();
    while (true) {
        Command c;
        while (xQueueReceive(queue_, &c, 0) == pdTRUE) {
            if (c.cmd == kCmdAttach) {
                DoAttach();
            } else if (!initialized_) {
                continue;
            } else if (c.cmd == kCmdMotion) {
                LoadMotion(c.motion);
            } else if (c.cmd == kCmdGlance) {
                LoadGlance(c.pan, c.tilt);
            }
        }

        int64_t now = NowMs();
        if (initialized_) {
            // 1) start the next keyframe
            if (frames_ != nullptr && !frame_active_ && frame_index_ < frame_count_) {
                const Keyframe& kf = frames_[frame_index_];
                for (int i = 0; i < AXIS_COUNT; ++i) {
                    if (kf.angle[i] == 0) continue;
                    axes_[i].target = Clamp(i, kf.angle[i]);
                    axes_[i].speed = kf.speed;
                }
                frame_active_ = true;
                frame_settled_ms_ = 0;
            }

            // 2) ramp both axes: accelerate, cruise, ease out near the target
            bool all_settled = true;
            for (int i = 0; i < AXIS_COUNT; ++i) {
                Axis& a = axes_[i];
                float d = a.target - a.current;
                if (std::fabs(d) < 0.3f) {
                    if (a.velocity != 0) {
                        a.current = a.target;
                        a.velocity = 0;
                        a.settled_ms = now;
                        WriteAngle(i, a.current);
                    }
                    continue;
                }
                all_settled = false;
                float ease = a.speed * std::fmax(0.2f, std::fmin(1.0f, std::fabs(d) / SERVO_EASE_DEG));
                a.velocity = std::fmin(a.velocity + SERVO_ACCEL_DEG_S2 * dt, std::fmin(a.speed, ease));
                float step = a.velocity * dt;
                a.current = std::fabs(d) <= step ? a.target : a.current + (d > 0 ? step : -step);
                WriteAngle(i, a.current);  // re-attaches a detached axis at its known position
            }

            // 3) keyframe hold / advance
            if (frame_active_ && all_settled) {
                if (frame_settled_ms_ == 0) frame_settled_ms_ = now;
                if (now - frame_settled_ms_ >= frames_[frame_index_].hold_ms) {
                    frame_active_ = false;
                    if (++frame_index_ >= frame_count_) {
                        frames_ = nullptr;
                        busy_ = false;
                    }
                }
            }

            // 4) cut PWM on axes that may rest unpowered once they hold still
            for (int i = 0; i < AXIS_COUNT; ++i) {
                Axis& a = axes_[i];
                if (kAxisConfig[i].detach_idle && a.attached && a.velocity == 0 &&
                    now - a.settled_ms >= SERVO_DETACH_AFTER_MS) {
                    Detach(i);
                }
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SERVO_TICK_MS));
    }
}
