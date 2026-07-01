#include <Arduino.h>
#include "speaker_controller.h"

static SpeakerController Speaker;

constexpr uint32_t PLAYBACK_POLL_MS = 20;
constexpr uint32_t PLAYBACK_DONE_GUARD_MS = 100;

const AudioId AUDIO_TEST_SEQUENCE[] = {
    AudioId::Boot,
    AudioId::PairOk,
    AudioId::PairFail,
    AudioId::ConnectionLost,
    AudioId::LowBattery,
    AudioId::Fault,
    AudioId::BeepSlow,
    AudioId::BeepMediumSlow,
    AudioId::BeepMedium,
    AudioId::BeepFast,
    AudioId::BeepContinuous,
};

constexpr uint8_t AUDIO_TEST_COUNT = sizeof(AUDIO_TEST_SEQUENCE) / sizeof(AUDIO_TEST_SEQUENCE[0]);

uint8_t audio_index = 0;
bool waiting_for_playback = false;
uint32_t playback_start_ms = 0;

void playNextAudio()
{
    const AudioId audio = AUDIO_TEST_SEQUENCE[audio_index];
    Speaker.playOnce(audio);

    playback_start_ms = millis();
    waiting_for_playback = true;
    audio_index = (audio_index + 1) % AUDIO_TEST_COUNT;
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    if (!Speaker.begin()) {
        Serial.println("Speaker begin failed. Check LittleFS and I2S setup.");
        while (true) {
            delay(1000);
        }
    }

    Speaker.setVolume(SpeakerVolumeLevel::Low);
    playNextAudio();
}

void loop()
{
    if (!waiting_for_playback) {
        playNextAudio();
        delay(PLAYBACK_POLL_MS);
        return;
    }

    const bool guard_elapsed = millis() - playback_start_ms >= PLAYBACK_DONE_GUARD_MS;
    if (guard_elapsed && Speaker.currentMode() == SpeakerMode::Silent) {
        waiting_for_playback = false;
    }

    delay(PLAYBACK_POLL_MS);
}
