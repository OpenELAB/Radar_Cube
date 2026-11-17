#ifndef SPEAKER_CONTROLLER_H
#define SPEAKER_CONTROLLER_H

#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ============================================================
// Speaker middle layer interface design draft
// ============================================================
//
// Design boundaries:
// - I2SClass + LittleFS + WAV parsing is responsible for actually playing the audio.
// - SpeakerController only provides a simple sound API to the main program.
// - The main program only calls playOnce / beep / stop and other interfaces.
// - Speaker task is the only place where WAV is actually read and I2S is written.
//
// It is assumed here that the "didi sound" is also a WAV file, and PWM/tone is no longer used directly to generate the sound.
// When the reversing radar is working, the sound expresses the current danger level, so the queue only retains the latest command.

// ======================== Configuration Macro ========================

#define SPEAKER_I2S_LRC_PIN                 21
#define SPEAKER_I2S_BCLK_PIN                22
#define SPEAKER_I2S_DIN_PIN                 23

#define SPEAKER_DEFAULT_VOLUME              50    // 0-100
#define SPEAKER_MAX_VOLUME                  100

#define SPEAKER_VOLUME_LOW_VALUE            30
#define SPEAKER_VOLUME_MED_VALUE            50
#define SPEAKER_VOLUME_HIGH_VALUE           75

#define SPEAKER_AUDIO_BUFFER_BYTES          1024
#define SPEAKER_TASK_STACK_WORDS            8192
#define SPEAKER_TASK_PRIORITY               1     // Default is no higher than Arduino loop / ESP-IDF main task; it can be changed to 2 after the main control is independent into a high-priority task
#define SPEAKER_COMMAND_QUEUE_LENGTH        1
#define SPEAKER_TASK_IDLE_DELAY_MS          10

// 5th gear periodic beeping sound. When the actual listening experience needs to be adjusted later, only these macros will be changed.
#define SPEAKER_BEEP_PERIOD_VERY_SLOW_MS    1500
#define SPEAKER_BEEP_PERIOD_SLOW_MS         1000
#define SPEAKER_BEEP_PERIOD_MEDIUM_MS       600
#define SPEAKER_BEEP_PERIOD_FAST_MS         300
#define SPEAKER_BEEP_PERIOD_DANGER_MS       180

// The WAV file name takes up space first, and the file under data/ can be replaced after the final audio is determined.
#define SPEAKER_FILE_BEEP_SHORT             "/beep_short.wav"
#define SPEAKER_FILE_BEEP_DANGER            "/beep_danger.wav"
#define SPEAKER_FILE_BOOT                   "/boot.wav"
#define SPEAKER_FILE_PAIR_OK                "/pair_ok.wav"
#define SPEAKER_FILE_PAIR_FAIL              "/pair_fail.wav"
#define SPEAKER_FILE_WAKE_FAIL              "/wake_fail.wav"
#define SPEAKER_FILE_LOW_BATTERY            "/low_battery.wav"
#define SPEAKER_FILE_FAULT                  "/fault.wav"

// ======================== Data structure ========================

enum class AudioId : uint8_t {
    None,
    BeepShort,      // Normal beep
    BeepDanger,     // Danger distance beep
    Boot,           // Power on/startup prompt
    PairOk,         // Pairing successful
    PairFail,       // Pairing failed
    WakeFail,       // Failed to wake up slave
    LowBattery,     // Low battery
    Fault           // Hardware/communication abnormality
};

enum class SpeakerMode : uint8_t {
    Silent,      // mute
    PlayOnce,    // Play WAV once
    Periodic     // Play WAV according to fixed gear cycle
};

enum class BeepLevel : uint8_t {
    VerySlow,    // Very slow, such as long distance
    Slow,        // slow
    Medium,      // medium speed
    Fast,        // fast
    Danger       // Danger, approaching and continuous prompt prompts
};

enum class SpeakerVolumeLevel : uint8_t {
    Default,     // Use current default volume
    Low,         // low volume
    Medium,      // Medium volume
    High         // high volume
};

struct SpeakerCommand {
    SpeakerMode mode = SpeakerMode::Silent;
    AudioId audio = AudioId::None;

    // Periodic mode is used, and ms is not directly exposed to the main program.
    BeepLevel level = BeepLevel::Slow;

    // Use the volume level, the specific value is determined by the SPEAKER_VOLUME_*_VALUE macro.
    SpeakerVolumeLevel volume = SpeakerVolumeLevel::Default;
};

struct WavInfo {
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_start = 0;
    uint32_t data_size = 0;
};

// ======================== Control class ========================

class SpeakerController {
public:
    SpeakerController();
    ~SpeakerController();

    // Initialize LittleFS, I2S related status, create command queue, and create speaker task.
    bool begin();

    // Stop speaker task, stop I2S, delete command queue.
    void end();

    // Set the default volume level, the specific value is determined by macro.
    bool setVolume(SpeakerVolumeLevel volume);
    SpeakerVolumeLevel volume() const;

    // Sound API used by the main program.
    // These interfaces are only responsible for delivering commands and do not directly play WAV.
    bool stop();
    bool playOnce(AudioId audio);
    bool beep(AudioId audio, BeepLevel level);

    bool isBegun() const;
    bool isTaskRunning() const;
    SpeakerMode currentMode() const;
    AudioId currentAudio() const;

private:
    I2SClass _i2s;

    QueueHandle_t _command_queue = nullptr;
    TaskHandle_t _task_handle = nullptr;

    SpeakerCommand _current_command;
    SpeakerMode _current_mode = SpeakerMode::Silent;
    AudioId _current_audio = AudioId::None;
    SpeakerVolumeLevel _current_volume = SpeakerVolumeLevel::Default;
    uint32_t _next_play_ms = 0;
    bool _begun = false;
    bool _task_running = false;
    bool _i2s_started = false;

    uint8_t _audio_buffer[SPEAKER_AUDIO_BUFFER_BYTES];
    int16_t _stereo_buffer[SPEAKER_AUDIO_BUFFER_BYTES];

    // Private task framework.
    // begin() creates a task internally, and end() deletes a task internally.
    static void taskEntry(void* arg);
    void taskLoop();

    // Unified delivery of commands, using xQueueOverwrite() internally to overwrite old commands.
    bool sendCommand(const SpeakerCommand& command);
};

// ============================================================
// Speaker task implementation suggestions
// ============================================================
//
// taskLoop() recommended structure:
//
// while (_task_running) {
//     SpeakerCommand new_cmd;
//     if (xQueueReceive(_command_queue, &new_cmd, 0) == pdTRUE) {
//         _current_command = new_cmd;
//         _current_mode = new_cmd.mode;
//         _current_audio = new_cmd.audio;
//         _next_play_ms = 0;
//     }
//
//     if (_current_mode == SpeakerMode::Silent) {
//         //Keep mute and stop current I2S output if necessary.
//     }
//
//     if (_current_mode == SpeakerMode::PlayOnce) {
//         //Play the WAV corresponding to _current_audio once.
//         //Switch back to Silent after playback is completed.
//     }
//
//     if (_current_mode == SpeakerMode::Periodic) {
//         // Get the cycle based on BeepLevel table lookup.
//         //Play the WAV corresponding to _current_audio once when the time is up.
//     }
//
//     vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
// }

// ============================================================
// Main program call example
// ============================================================
//
// #include "speaker_controller.h"
//
// static SpeakerController Speaker;
//
// void setup()
// {
//     Speaker.begin();
//     Speaker.setVolume(SpeakerVolumeLevel::Medium);
//     Speaker.playOnce(AudioId::Boot);
// }
//
// void onPairSuccess()
// {
//     Speaker.playOnce(AudioId::PairOk);
// }
//
// void onWakeFail()
// {
//     Speaker.playOnce(AudioId::WakeFail);
// }
//
// void updateParkingDistance(uint16_t distance_cm)
// {
//     if (distance_cm > 220) {
//         Speaker.stop();
//     } else if (distance_cm > 170) {
//         Speaker.beep(AudioId::BeepShort, BeepLevel::VerySlow);
//     } else if (distance_cm > 120) {
//         Speaker.beep(AudioId::BeepShort, BeepLevel::Medium);
//     } else if (distance_cm > 70) {
//         Speaker.beep(AudioId::BeepShort, BeepLevel::Fast);
//     } else {
//         Speaker.beep(AudioId::BeepDanger, BeepLevel::Danger);
//     }
// }
//
// void beforeDeepSleep()
// {
//     Speaker.stop();
//     Speaker.end();
// }

#endif
