#include "radar_audio_manager.h"

#include "config.h"

void RadarAudioManager::begin(SpeakerController& speaker)
{
    _speaker = &speaker;
}

bool RadarAudioManager::queue(RadarAudioId audio,
                              SpeakerVolumeLevel volume,
                              RadarEventSeverity severity,
                              RadarEventTarget target)
{
    // 第一阶段还没有左右/双侧专用 WAV，target 只保留语义，不参与播放决策。
    (void)target;

    if (severity == RadarEventSeverity::RealtimeDistance) {
        ESP_LOGW(MAIN_TAG, "Distance audio must use updateDistance(): %s",
                 radarAudioName(audio));
        return false;
    }

    RadarAudioPrompt prompt;
    prompt.audio = audio;
    prompt.volume = volume;
    prompt.priority = priorityForSeverity(severity);
    return pushPrompt(prompt);
}

void RadarAudioManager::update()
{
    if (_speaker == nullptr || !_speaker->isBegun()) {
        return;
    }

    if (_prompt_playing) {
        if (_speaker->currentMode() == SpeakerMode::Silent &&
            millis() - _prompt_started_ms < (SPEAKER_TASK_IDLE_DELAY_MS * 3)) {
            return;
        }
        if (_speaker->currentMode() != SpeakerMode::Silent) {
            return;
        }
        _prompt_playing = false;
        _current_prompt_priority = AudioPriority::P4;
        // 提示音结束后恢复工作音量，保证后续距离蜂鸣仍然清晰。
        _speaker->setVolume(SpeakerVolumeLevel::High);
    }

    RadarAudioPrompt prompt;
    if (!popNextPrompt(distanceActive(), prompt)) {
        return;
    }

    if (distanceActive()) {
        // P0 提示音抢占距离蜂鸣时，先清理业务层的距离状态；
        // 真正的播放中断由 SpeakerController 的覆盖式命令队列完成。
        _current_distance = RadarAudioId::None;
        _pending_distance = RadarAudioId::None;
        _pending_distance_count = 0;
        _last_distance_switch_ms = millis();
    }

    const char* path = radarAudioPath(prompt.audio);
    _speaker->setVolume(prompt.volume);
    if (_speaker->playOnce(path)) {
        _prompt_playing = true;
        _current_prompt_priority = prompt.priority;
        _prompt_started_ms = millis();
        ESP_LOGI(MAIN_TAG, "Audio prompt playing: %s", radarAudioName(prompt.audio));
    } else {
        _current_prompt_priority = AudioPriority::P4;
        _speaker->setVolume(SpeakerVolumeLevel::High);
    }
}

bool RadarAudioManager::busy() const
{
    return _prompt_playing || _queue_count > 0;
}

void RadarAudioManager::drain(uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    while (busy() && millis() - start_ms < timeout_ms) {
        update();
        vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
    }
}

void RadarAudioManager::updateDistance(uint16_t min_dist_cm)
{
    if (_speaker == nullptr || !_speaker->isBegun()) {
        return;
    }

    if (blocksDistance()) {
        // P0 安全提示正在播放时，距离蜂鸣必须让路。
        return;
    }

    const RadarAudioId target = distanceToAudio(min_dist_cm);
    if (target == _current_distance) {
        _pending_distance = RadarAudioId::None;
        _pending_distance_count = 0;
        return;
    }

    if (target != _pending_distance) {
        _pending_distance = target;
        _pending_distance_count = 1;
        return;
    }

    if (_pending_distance_count < RADAR_AUDIO_DISTANCE_CONFIRM_COUNT) {
        _pending_distance_count++;
        return;
    }

    if (millis() - _last_distance_switch_ms < RADAR_AUDIO_DISTANCE_MIN_SWITCH_MS) {
        return;
    }

    if (target == RadarAudioId::None) {
        stopDistance();
        return;
    }

    if (_prompt_playing && !canInterruptDistance(_current_prompt_priority)) {
        // 距离蜂鸣优先于 P2-P4 普通提示；这里让实时距离状态重新接管声道。
        _prompt_playing = false;
        _current_prompt_priority = AudioPriority::P4;
        _speaker->setVolume(SpeakerVolumeLevel::High);
    }

    const char* path = radarAudioPath(target);
    if (_speaker->playLoop(path)) {
        _current_distance = target;
        _pending_distance = RadarAudioId::None;
        _pending_distance_count = 0;
        _last_distance_switch_ms = millis();
        ESP_LOGI(MAIN_TAG, "Distance beep playing: %s, dist=%u cm",
                 radarAudioName(target), min_dist_cm);
    }
}

void RadarAudioManager::stopDistance()
{
    if (_speaker == nullptr || _current_distance == RadarAudioId::None) {
        return;
    }

    _speaker->stop();
    ESP_LOGI(MAIN_TAG, "Distance beep stopped");
    _current_distance = RadarAudioId::None;
    _pending_distance = RadarAudioId::None;
    _pending_distance_count = 0;
    _last_distance_switch_ms = millis();
}

bool RadarAudioManager::distanceActive() const
{
    return _current_distance != RadarAudioId::None;
}

bool RadarAudioManager::blocksDistance() const
{
    return _prompt_playing && canInterruptDistance(_current_prompt_priority);
}

bool RadarAudioManager::priorityHigher(AudioPriority left, AudioPriority right)
{
    return static_cast<uint8_t>(left) < static_cast<uint8_t>(right);
}

bool RadarAudioManager::canInterruptDistance(AudioPriority priority)
{
    return priority == AudioPriority::P0;
}

AudioPriority RadarAudioManager::priorityForSeverity(RadarEventSeverity severity)
{
    switch (severity) {
    case RadarEventSeverity::SafetyCritical:
        return AudioPriority::P0;
    case RadarEventSeverity::RealtimeDistance:
        return AudioPriority::P1;
    case RadarEventSeverity::WorkWarning:
        return AudioPriority::P2;
    case RadarEventSeverity::FlowResult:
        return AudioPriority::P3;
    case RadarEventSeverity::Lifecycle:
    default:
        return AudioPriority::P4;
    }
}

RadarAudioId RadarAudioManager::distanceToAudio(uint16_t dist_cm)
{
    if (dist_cm == UINT16_MAX || dist_cm > 150) {
        return RadarAudioId::None;
    }
    if (dist_cm > 120) {
        return RadarAudioId::DistBeepFar;
    }
    if (dist_cm > 90) {
        return RadarAudioId::DistBeepMidFar;
    }
    if (dist_cm > 60) {
        return RadarAudioId::DistBeepMid;
    }
    if (dist_cm > 30) {
        return RadarAudioId::DistBeepNear;
    }
    return RadarAudioId::DistBeepDanger;
}

bool RadarAudioManager::pushPrompt(const RadarAudioPrompt& prompt)
{
    if (prompt.audio == RadarAudioId::None || radarAudioPath(prompt.audio) == nullptr) {
        ESP_LOGW(MAIN_TAG, "Audio prompt dropped: %s", radarAudioName(prompt.audio));
        return false;
    }

    if (_queue_count >= RADAR_AUDIO_PROMPT_QUEUE_LENGTH) {
        uint8_t worst_index = 0;
        for (uint8_t i = 1; i < _queue_count; i++) {
            if (priorityHigher(_queue[worst_index].priority, _queue[i].priority)) {
                worst_index = i;
            }
        }

        if (!priorityHigher(prompt.priority, _queue[worst_index].priority)) {
            ESP_LOGW(MAIN_TAG, "Audio prompt dropped: %s", radarAudioName(prompt.audio));
            return false;
        }

        ESP_LOGW(MAIN_TAG, "Audio prompt replaced: %s",
                 radarAudioName(_queue[worst_index].audio));
        for (uint8_t i = worst_index; i + 1 < _queue_count; i++) {
            _queue[i] = _queue[i + 1];
        }
        _queue_count--;
    }

    _queue[_queue_count] = prompt;
    _queue_count++;
    ESP_LOGI(MAIN_TAG, "Audio prompt queued: %s", radarAudioName(prompt.audio));
    return true;
}

bool RadarAudioManager::popNextPrompt(bool distance_active, RadarAudioPrompt& prompt)
{
    int8_t best_index = -1;
    for (uint8_t i = 0; i < _queue_count; i++) {
        if (distance_active && !canInterruptDistance(_queue[i].priority)) {
            // 距离蜂鸣活跃时，只允许 P0 从队列中抢占播放。
            continue;
        }
        if (best_index < 0 || priorityHigher(_queue[i].priority, _queue[best_index].priority)) {
            best_index = i;
        }
    }

    if (best_index < 0) {
        return false;
    }

    prompt = _queue[best_index];
    for (uint8_t i = best_index; i + 1 < _queue_count; i++) {
        _queue[i] = _queue[i + 1];
    }
    _queue_count--;
    return true;
}
