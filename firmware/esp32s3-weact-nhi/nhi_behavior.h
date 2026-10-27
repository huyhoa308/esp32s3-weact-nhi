// NhiBehavior: maps device state + server emotions to the eyes and the head.
// All mapping tables live in nhi_behavior.cc.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "device_state.h"
#include "eyes_display.h"
#include "head_servos.h"

class NhiBehavior {
public:
    NhiBehavior(EyesDisplay* eyes, HeadServos* head);
    void Start();

    // Called from EyesDisplay hooks (any task). Non-blocking.
    void OnEmotion(const char* emotion);
    void OnChat(const char* role, const char* content);

private:
    enum class EventType : uint8_t { kEmotion, kUserText };
    struct Event {
        EventType type;
        char text[24];
    };

    static void TaskEntry(void* arg);
    void Loop();
    void HandleState(DeviceState state, int64_t now);
    void HandleEmotion(const char* emotion, int64_t now);
    void IdleFidget(int64_t now);
    void ScheduleFidget(int64_t now);

    EyesDisplay* eyes_;
    HeadServos* head_;
    QueueHandle_t events_ = nullptr;
    TaskHandle_t task_ = nullptr;

    DeviceState state_ = kDeviceStateUnknown;
    bool servos_started_ = false;
    bool thinking_ = false;
    bool sleepy_ = false;
    bool emotion_active_ = false;  // a server emotion is showing (fades after a while)
    bool nod_next_ = true;         // speaking: alternate nod / sway
    bool head_glancing_ = false;   // idle fidget turned the head, re-centre at its end
    int fidget_count_ = 0;
    int64_t state_since_ms_ = 0;
    int64_t last_speak_move_ms_ = 0;
    int64_t emotion_since_ms_ = 0;
    int64_t surprise_until_ms_ = 0;
    int64_t next_fidget_ms_ = 0;
    int64_t fidget_end_ms_ = 0;
};
