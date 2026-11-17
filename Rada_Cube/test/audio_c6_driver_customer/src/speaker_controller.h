#ifndef SPEAKER_CONTROLLER_H
#define SPEAKER_CONTROLLER_H

#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ============================================================
// Speaker middle layer interface
// ============================================================
//
// Design boundaries:
// - I2SClass + LittleFS + WAV parsing is responsible for actually playing the audio.
// - SpeakerController only provides a simple sound API to the main program.
// - The main program only calls playOnce / playLoop / stop and other interfaces.
// - Speaker task is the only place where WAV is actually read and I2S is written.
//
// Current beep rhythm scheme:
// - No more code spelling out different beep periods with timers.
// - Different speed/rhythm beeps are made into different WAV files.
// - playLoop() repeats the specified WAV; the WAV content determines the cycle length.
// - The command queue length is 1, using overlay delivery, and only retaining the latest voice command.

// ======================== Hardware and task configuration ========================

// I2S pin.
#define SPEAKER_I2S_LRC_PIN                 19
#define SPEAKER_I2S_BCLK_PIN                20
#define SPEAKER_I2S_DIN_PIN                 21
#define SPEAKER_SHUTDOWN_PIN                7

// Amplifier shutdown control.
#define SPEAKER_SHUTDOWN_ACTIVE_LEVEL       LOW
#define SPEAKER_ENABLE_SETTLE_MS            5     // Wait for the stabilization time after the power amplifier is enabled.
#define SPEAKER_END_SILENCE_MS              80    // Add silence after a single playback to avoid popping sounds at the end
#define SPEAKER_SHUTDOWN_SETTLE_MS          2     // Stable waiting time after turning off the amplifier
#define SPEAKER_IDLE_SHUTDOWN_MS            500   // How long to wait for silent idle time before turning off the I2S/amplifier

// Software volume, range 0-100.
#define SPEAKER_DEFAULT_VOLUME              50
#define SPEAKER_MAX_VOLUME                  100

#define SPEAKER_VOLUME_LOW_VALUE            30
#define SPEAKER_VOLUME_MED_VALUE            50
#define SPEAKER_VOLUME_HIGH_VALUE           75

// Playback tasks and audio buffering configuration.
#define SPEAKER_AUDIO_BUFFER_BYTES          1024
#define SPEAKER_TASK_STACK_WORDS            8192
#define SPEAKER_TASK_PRIORITY               1     // Default is no higher than Arduino loop / ESP-IDF main task
#define SPEAKER_COMMAND_QUEUE_LENGTH        1
#define SPEAKER_TASK_IDLE_DELAY_MS          10

// ======================== WAV file configuration ========================
//
// The beep cycle has been built into the WAV file:
// - beep_slow.wav slow beep
// - beep_medium_slow.wav medium slow beep
// - beep_medium.wav medium speed beep
// - beep_fast.wav fast beep
// - beep_continuous.wav Continuous/danger beep
//
// Replace the WAV file with the same name under data/ to adjust the timbre, frequency, envelope and period.
#define SPEAKER_FILE_BEEP_SLOW              "/beep_slow.wav"
#define SPEAKER_FILE_BEEP_MEDIUM_SLOW       "/beep_medium_slow.wav"
#define SPEAKER_FILE_BEEP_MEDIUM            "/beep_medium.wav"
#define SPEAKER_FILE_BEEP_FAST              "/beep_fast.wav"
#define SPEAKER_FILE_BEEP_CONTINUOUS        "/beep_continuous.wav"
#define SPEAKER_FILE_BOOT                   "/boot.wav"
#define SPEAKER_FILE_PAIR_OK                "/pair_ok.wav"
#define SPEAKER_FILE_PAIR_FAIL              "/pair_fail.wav"
#define SPEAKER_FILE_CONNECTION_LOST        "/connection_lost.wav"
#define SPEAKER_FILE_LOW_BATTERY            "/low_battery.wav"
#define SPEAKER_FILE_FAULT                  "/fault.wav"

// ======================== Data structure ========================

enum class AudioId : uint8_t {
    None,
    BeepSlow,       // slow beep
    BeepMediumSlow, // Medium and slow beep
    BeepMedium,     // Medium speed beep
    BeepFast,       // rapid beep
    BeepContinuous, // Continuous/danger beep
    Boot,           // Power on/startup prompt
    PairOk,         // Pairing successful
    PairFail,       // Pairing failed
    ConnectionLost, // Connection lost
    LowBattery,     // Low battery
    Fault           // Hardware/communication abnormality
};

enum class SpeakerMode : uint8_t {
    Silent,      // mute
    PlayOnce,    // Play WAV once
    Loop         // Play WAV in loop
};

enum class SpeakerVolumeLevel : uint8_t {
    Default,     // Use default volume
    Low,         // low volume
    Medium,      // Medium volume
    High         // high volume
};

struct SpeakerCommand {
    SpeakerMode mode = SpeakerMode::Silent;
    AudioId audio = AudioId::None;

    // Currently reserved as a command field, the global volume set by setVolume() is actually used by default.
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

    // Initialize LittleFS, command queue and speaker task.
    bool begin();

    // Stop speaker task, turn off I2S/amplifier and release queue.
    void end();

    // Set the default volume level.
    bool setVolume(SpeakerVolumeLevel volume);
    SpeakerVolumeLevel volume() const;

    // Sound API used by the main program.
    // These interfaces are only responsible for delivering commands and do not directly read files or write I2S.
    bool stop();
    bool playOnce(AudioId audio);
    bool playLoop(AudioId audio);

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
    bool _begun = false;
    bool _task_running = false;
    bool _i2s_started = false;
    bool _speaker_enabled = false;
    uint32_t _i2s_sample_rate = 0;
    uint32_t _last_output_ms = 0;

    uint8_t _audio_buffer[SPEAKER_AUDIO_BUFFER_BYTES];
    int16_t _stereo_buffer[SPEAKER_AUDIO_BUFFER_BYTES];

    // Private task framework. begin() creates a task, end() stops and deletes a task.
    static void taskEntry(void* arg);
    void taskLoop();

    // Unified delivery of commands, using xQueueOverwrite() internally to overwrite old commands.
    bool sendCommand(const SpeakerCommand& command);

    const char* audioFilePath(AudioId audio) const;
    uint8_t volumeValue(SpeakerVolumeLevel volume) const;
    bool readWavInfo(File& file, WavInfo& info);
    bool startI2S(const WavInfo& info);
    void stopI2S();
    void setSpeakerEnabled(bool enabled);
    void writeSilence(uint32_t sample_rate, uint16_t duration_ms);
    void shutdownAudioOutput();
    bool playWav(AudioId audio, bool append_end_silence);
    void writeAudioChunk(uint8_t* data, size_t bytes_read, const WavInfo& info);
    int16_t applyVolume(int16_t sample) const;
};

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
// void onConnectionLost()
// {
//     Speaker.playOnce(AudioId::ConnectionLost);
// }
//
// void updateParkingDistance(uint16_t distance_cm)
// {
//     if (distance_cm > 220) {
//         Speaker.stop();
//     } else if (distance_cm > 170) {
//         Speaker.playLoop(AudioId::BeepSlow);
//     } else if (distance_cm > 120) {
//         Speaker.playLoop(AudioId::BeepMediumSlow);
//     } else if (distance_cm > 70) {
//         Speaker.playLoop(AudioId::BeepMedium);
//     } else if (distance_cm > 30) {
//         Speaker.playLoop(AudioId::BeepFast);
//     } else {
//         Speaker.playLoop(AudioId::BeepContinuous);
//     }
// }
//
// void beforeDeepSleep()
// {
//     Speaker.stop();
//     Speaker.end();
// }

#endif
