#ifndef RADAR_AUDIO_MANAGER_H
#define RADAR_AUDIO_MANAGER_H

#include <Arduino.h>
#include "radar_audio_catalog.h"
#include "speaker_controller.h"

#ifndef RADAR_AUDIO_PROMPT_QUEUE_LENGTH
#define RADAR_AUDIO_PROMPT_QUEUE_LENGTH       8
#endif
#ifndef RADAR_AUDIO_PROMPT_DRAIN_TIMEOUT_MS
#define RADAR_AUDIO_PROMPT_DRAIN_TIMEOUT_MS   5000
#endif
#ifndef RADAR_AUDIO_DISTANCE_CONFIRM_COUNT
#define RADAR_AUDIO_DISTANCE_CONFIRM_COUNT    2
#endif
#ifndef RADAR_AUDIO_DISTANCE_MIN_SWITCH_MS
#define RADAR_AUDIO_DISTANCE_MIN_SWITCH_MS    350
#endif

// 音效是单通道资源：同一时刻只能有一个声音真正播放。
// P0 可以抢占距离蜂鸣；P1 距离蜂鸣不进入普通提示队列。
enum class AudioPriority : uint8_t {
    P0 = 0,
    P1 = 1,
    P2 = 2,
    P3 = 3,
    P4 = 4
};

// 业务事件语义层，只描述事件严重程度，不直接决定具体 WAV 文件名。
enum class RadarEventSeverity : uint8_t {
    SafetyCritical,
    RealtimeDistance,
    WorkWarning,
    FlowResult,
    Lifecycle
};

// 当前阶段仍使用通用音效，target 先保留给第二阶段左右/双侧语音扩展。
enum class RadarEventTarget : uint8_t {
    Inside,
    Left,
    Right,
    Both,
    Nearest
};

struct RadarAudioPrompt {
    RadarAudioId audio = RadarAudioId::None;
    SpeakerVolumeLevel volume = SpeakerVolumeLevel::Default;
    AudioPriority priority = AudioPriority::P4;
};

class RadarAudioManager {
public:
    void begin(SpeakerController& speaker);

    // 普通提示入口：上层传入业务音效 ID 和事件语义，本类负责映射成播放优先级。
    bool queue(RadarAudioId audio,
               SpeakerVolumeLevel volume,
               RadarEventSeverity severity,
               RadarEventTarget target = RadarEventTarget::Inside);

    void update();
    bool busy() const;
    void drain(uint32_t timeout_ms = RADAR_AUDIO_PROMPT_DRAIN_TIMEOUT_MS);

    // 距离蜂鸣是实时状态，不进入普通提示队列，避免历史蜂鸣排队滞后播放。
    void updateDistance(uint16_t min_dist_cm);
    void stopDistance();
    bool distanceActive() const;
    bool blocksDistance() const;

private:
    SpeakerController* _speaker = nullptr;
    RadarAudioPrompt _queue[RADAR_AUDIO_PROMPT_QUEUE_LENGTH];
    uint8_t _queue_count = 0;
    bool _prompt_playing = false;
    AudioPriority _current_prompt_priority = AudioPriority::P4;
    uint32_t _prompt_started_ms = 0;
    RadarAudioId _current_distance = RadarAudioId::None;
    RadarAudioId _pending_distance = RadarAudioId::None;
    uint8_t _pending_distance_count = 0;
    uint32_t _last_distance_switch_ms = 0;

    static bool priorityHigher(AudioPriority left, AudioPriority right);
    static bool canInterruptDistance(AudioPriority priority);
    static AudioPriority priorityForSeverity(RadarEventSeverity severity);
    static RadarAudioId distanceToAudio(uint16_t dist_cm);

    bool pushPrompt(const RadarAudioPrompt& prompt);
    bool popNextPrompt(bool distance_active, RadarAudioPrompt& prompt);
};

#endif
