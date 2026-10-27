#include "nhi_behavior.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <cstring>

#include "application.h"
#include "nhi_config.h"

#define TAG "NhiBehavior"

// ============================================================================
// THE mapping table: server emotion -> eyes + head.
// Unknown strings fall back to kNeutral (never crash).
//
//   Vui vẻ     mắt cười, vàng       lắc trái/phải      (happy_shake)
//   Buồn       mắt cụp, xanh        cúi đầu chậm       (sad_droop)
//   Tức giận   mắt cau, đỏ          lắc đầu ngắn       (angry_shake)
//   Ngạc nhiên mở to, "!"           ngẩng lên          (look_up)
//   Suy nghĩ   nhìn lệch, "?"       quay lệch một phía (tilt_side)
//   Buồn ngủ   lim dim, Zzz         cúi nhẹ rồi giữ    (sleep_droop)
// ============================================================================
namespace {
struct EmotionRule {
    const char* emotion;
    EyePreset eyes;
    HeadMotion head;
};

const EmotionRule kEmotionTable[] = {
    // server emotions (xiaozhi LLM emoji set)
    {"happy", EyePreset::kHappy, HeadMotion::kHappyShake},
    {"laughing", EyePreset::kLaugh, HeadMotion::kHappyShake},
    {"funny", EyePreset::kLaugh, HeadMotion::kHappyShake},
    {"loving", EyePreset::kLove, HeadMotion::kNod},
    {"kissy", EyePreset::kKiss, HeadMotion::kNod},
    {"delicious", EyePreset::kDelicious, HeadMotion::kHappyShake},
    {"sad", EyePreset::kSad, HeadMotion::kSadDroop},
    {"crying", EyePreset::kCry, HeadMotion::kSadDroop},
    {"embarrassed", EyePreset::kEmbarrassed, HeadMotion::kSadDroop},
    {"angry", EyePreset::kAngry, HeadMotion::kAngryShake},
    {"surprised", EyePreset::kSurprised, HeadMotion::kLookUp},
    {"shocked", EyePreset::kShocked, HeadMotion::kLookUp},
    {"confused", EyePreset::kConfused, HeadMotion::kTiltSide},
    {"thinking", EyePreset::kThinking, HeadMotion::kTiltSide},
    {"sleepy", EyePreset::kSleepy, HeadMotion::kSleepDroop},
    {"relaxed", EyePreset::kRelaxed, HeadMotion::kSway},
    {"winking", EyePreset::kWink, HeadMotion::kNod},
    {"cool", EyePreset::kCool, HeadMotion::kCenter},
    {"confident", EyePreset::kCool, HeadMotion::kCenter},
    {"silly", EyePreset::kSilly, HeadMotion::kHappyShake},
    // device-side emotions sent by the firmware core
    {"robot_2", EyePreset::kBoot, HeadMotion::kNone},
    {"warning", EyePreset::kError, HeadMotion::kNone},
    {"cloud_off", EyePreset::kError, HeadMotion::kNone},
    {"cancel", EyePreset::kError, HeadMotion::kNone},
    {"gear", EyePreset::kError, HeadMotion::kNone},          // Wi-Fi provisioning
    {"link", EyePreset::kNeutral, HeadMotion::kNone},        // activation code shown
    {"download", EyePreset::kThinking, HeadMotion::kNone},   // OTA
    {"cloud_download", EyePreset::kThinking, HeadMotion::kNone},
};

// ============================================================================
// Device state -> eyes + head
// ============================================================================
struct StateRule {
    DeviceState state;
    EyePreset eyes;
    HeadMotion head;
};

const StateRule kStateTable[] = {
    {kDeviceStateStarting, EyePreset::kBoot, HeadMotion::kNone},
    {kDeviceStateIdle, EyePreset::kIdle, HeadMotion::kCenter},
    {kDeviceStateConnecting, EyePreset::kThinking, HeadMotion::kNone},
    {kDeviceStateListening, EyePreset::kListening, HeadMotion::kListenUp},
    {kDeviceStateSpeaking, EyePreset::kSpeaking, HeadMotion::kNod},
    {kDeviceStateWifiConfiguring, EyePreset::kError, HeadMotion::kNone},
    {kDeviceStateActivating, EyePreset::kNeutral, HeadMotion::kNone},
    {kDeviceStateUpgrading, EyePreset::kThinking, HeadMotion::kNone},
    {kDeviceStateFatalError, EyePreset::kError, HeadMotion::kRelax},
};
// "Thinking" is not an upstream state: Listening + user text received.
constexpr EyePreset kThinkingEyes = EyePreset::kThinking;
constexpr HeadMotion kThinkingHead = HeadMotion::kTiltSide;

// Short random expressions while idle so the robot looks alive. Looks are
// "from the robot": eyes drawn on the viewer's left (W) = robot looks to its
// right = PAN below neutral.
enum class Fidget : uint8_t { kLookLeft, kLookRight, kLookUp, kBlink, kWink, kHappy, kCurious };

int64_t now_ms() { return esp_timer_get_time() / 1000; }
int64_t rand_between(int64_t lo, int64_t hi) { return lo + (int64_t)(esp_random() % (uint32_t)(hi - lo + 1)); }
}  // namespace

NhiBehavior::NhiBehavior(EyesDisplay* eyes, HeadServos* head) : eyes_(eyes), head_(head) {
    events_ = xQueueCreate(8, sizeof(Event));
}

void NhiBehavior::Start() {
    eyes_->SetEmotionHook([this](const char* e) { OnEmotion(e); });
    eyes_->SetChatHook([this](const char* role, const char* content) { OnChat(role, content); });
    eyes_->ApplyPreset(EyePreset::kBoot);
    xTaskCreate(&NhiBehavior::TaskEntry, "nhi", 4096, this, 3, &task_);
}

void NhiBehavior::OnEmotion(const char* emotion) {
    Event ev = {};
    ev.type = EventType::kEmotion;
    strncpy(ev.text, emotion ? emotion : "", sizeof(ev.text) - 1);
    xQueueSend(events_, &ev, 0);
}

void NhiBehavior::OnChat(const char* role, const char* content) {
    if (role == nullptr || strcmp(role, "user") != 0 || content == nullptr || content[0] == 0) return;
    Event ev = {};
    ev.type = EventType::kUserText;
    xQueueSend(events_, &ev, 0);
}

void NhiBehavior::TaskEntry(void* arg) { static_cast<NhiBehavior*>(arg)->Loop(); }

void NhiBehavior::ScheduleFidget(int64_t now) {
    next_fidget_ms_ = now + rand_between(NHI_IDLE_FIDGET_MIN_MS, NHI_IDLE_FIDGET_MAX_MS);
}

void NhiBehavior::HandleState(DeviceState state, int64_t now) {
    ESP_LOGI(TAG, "state %d -> %d", (int)state_, (int)state);
    DeviceState prev = state_;
    state_ = state;
    state_since_ms_ = now;
    thinking_ = false;
    sleepy_ = false;
    surprise_until_ms_ = 0;
    fidget_end_ms_ = 0;
    head_glancing_ = false;
    ScheduleFidget(now);

    eyes_->SetSpeaking(state == kDeviceStateSpeaking);

    // Servos are powered only once Wi-Fi and audio are up (first Idle).
    if (state == kDeviceStateIdle && !servos_started_) {
        servos_started_ = true;
        head_->AttachSequential();
    }

    // After a conversation, keep the last emotion (eyes and head pose) for a
    // moment instead of snapping straight back to the idle look.
    if (state == kDeviceStateIdle && prev == kDeviceStateSpeaking && emotion_active_) {
        emotion_since_ms_ = now;
        return;
    }
    if (state != kDeviceStateSpeaking) emotion_active_ = false;

    for (const auto& r : kStateTable) {
        if (r.state != state) continue;
        eyes_->ApplyPreset(r.eyes);
        head_->Play(r.head);
        if (state == kDeviceStateSpeaking) {
            last_speak_move_ms_ = now;
            nod_next_ = false;  // the state rule just nodded, sway next
        }
        return;
    }
}

void NhiBehavior::HandleEmotion(const char* emotion, int64_t now) {
    if (strcmp(emotion, "neutral") == 0 || emotion[0] == 0) {
        // The core sends "neutral" on every idle/listening/connecting change;
        // the state table owns those looks. Only reset the mood while speaking.
        if (state_ == kDeviceStateSpeaking) {
            eyes_->ApplyPreset(EyePreset::kNeutral);
            emotion_active_ = false;
        }
        return;
    }
    for (const auto& r : kEmotionTable) {
        if (strcmp(r.emotion, emotion) != 0) continue;
        eyes_->ApplyPreset(r.eyes);
        if (r.eyes == EyePreset::kSurprised) surprise_until_ms_ = now + NHI_SURPRISE_MS;
        head_->Play(r.head);
        if (r.head != HeadMotion::kNone) last_speak_move_ms_ = now;  // don't overlap a nod
        emotion_active_ = true;
        emotion_since_ms_ = now;
        return;
    }
    ESP_LOGW(TAG, "unknown emotion '%s' -> neutral", emotion);
    eyes_->ApplyPreset(EyePreset::kNeutral);
}

void NhiBehavior::IdleFidget(int64_t now) {
    // Only some looks also turn the head, so the servos rest most of the time.
    bool move_head = head_->attached() && (++fidget_count_ % NHI_IDLE_HEAD_EVERY_N) == 0;
    auto f = (Fidget)(esp_random() % 7);
    switch (f) {
        case Fidget::kLookLeft:
            eyes_->LookAt(roboeyes::W);
            if (move_head) head_->Glance(-NHI_GLANCE_PAN_DEG, 0);
            break;
        case Fidget::kLookRight:
            eyes_->LookAt(roboeyes::E);
            if (move_head) head_->Glance(NHI_GLANCE_PAN_DEG, 0);
            break;
        case Fidget::kLookUp:
            eyes_->LookAt(roboeyes::N);
            if (move_head) head_->Glance(0, NHI_GLANCE_TILT_DEG);
            break;
        case Fidget::kBlink: eyes_->Blink(); move_head = false; break;
        case Fidget::kWink: eyes_->Wink(); move_head = false; break;
        case Fidget::kHappy: eyes_->ApplyPreset(EyePreset::kHappy); move_head = false; break;
        case Fidget::kCurious:
            eyes_->ApplyPreset(EyePreset::kSurprised);
            if (move_head) head_->Glance(0, NHI_GLANCE_TILT_DEG / 2);
            break;
    }
    head_glancing_ = move_head;
    fidget_end_ms_ = now + 1800;
    ScheduleFidget(now);
}

void NhiBehavior::Loop() {
    // Let Board/Application construction finish before touching the singleton.
    vTaskDelay(pdMS_TO_TICKS(500));
    auto& app = Application::GetInstance();
    TickType_t last = xTaskGetTickCount();
    while (true) {
        int64_t now = now_ms();
        DeviceState s = app.GetDeviceState();
        if (s != state_) HandleState(s, now);

        Event ev;
        while (xQueueReceive(events_, &ev, 0) == pdTRUE) {
            if (ev.type == EventType::kEmotion) {
                HandleEmotion(ev.text, now);
            } else if (ev.type == EventType::kUserText && state_ == kDeviceStateListening &&
                       !thinking_) {
                thinking_ = true;
                eyes_->ApplyPreset(kThinkingEyes);
                head_->Play(kThinkingHead);
            }
        }

        if (surprise_until_ms_ != 0 && now > surprise_until_ms_) {
            surprise_until_ms_ = 0;
            eyes_->SetCuriosity(false);
        }

        // Speaking: small nod or sway every few seconds, never on top of an emotion move.
        if (state_ == kDeviceStateSpeaking && !head_->busy() &&
            now - last_speak_move_ms_ > NHI_SPEAK_NOD_EVERY_MS) {
            last_speak_move_ms_ = now;
            head_->Play(nod_next_ ? HeadMotion::kNod : HeadMotion::kSway);
            nod_next_ = !nod_next_;
        }

        if (state_ == kDeviceStateIdle) {
            if (emotion_active_ && now - emotion_since_ms_ > NHI_EMOTION_HOLD_MS / 2) {
                emotion_active_ = false;  // post-chat emotion has been shown long enough
                eyes_->ApplyPreset(EyePreset::kIdle);
                head_->Play(HeadMotion::kCenter);
                ScheduleFidget(now);
            } else if (!emotion_active_) {
                if (fidget_end_ms_ != 0 && now > fidget_end_ms_) {
                    fidget_end_ms_ = 0;
                    eyes_->ApplyPreset(sleepy_ ? EyePreset::kSleepy : EyePreset::kIdle);
                    if (head_glancing_) {
                        head_glancing_ = false;
                        head_->Glance(0, 0);  // slow return to centre
                    }
                }
                if (!sleepy_ && now - state_since_ms_ > NHI_IDLE_SLEEPY_AFTER_MS) {
                    sleepy_ = true;
                    eyes_->ApplyPreset(EyePreset::kSleepy);
                    head_->Play(HeadMotion::kSleepDroop);  // then held still
                } else if (!sleepy_ && fidget_end_ms_ == 0 && now > next_fidget_ms_) {
                    IdleFidget(now);
                }
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(NHI_POLL_MS));
    }
}
