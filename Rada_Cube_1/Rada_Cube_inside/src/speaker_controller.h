#ifndef SPEAKER_CONTROLLER_H
#define SPEAKER_CONTROLLER_H

#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pins.h"

// The software volume range is 0..100, and the default value is restored every time begin() is called.
#define SPEAKER_DEFAULT_VOLUME 50
#define SPEAKER_MAX_VOLUME 100

// The physical play mode that the task has actually applied does not represent the queued commands that have not yet been consumed.
enum SpeakerMode : uint8_t {
    SPEAKER_MODE_SILENT = 0,
    SPEAKER_MODE_PLAY_ONCE,
    SPEAKER_MODE_LOOP
};

/*
 * Speaker audio hardware controller.
 *
 * SpeakerController only performs physical audio playback of the path specified by the caller and does not understand business events.
 * AudioId, path mapping or business priority. The only speaker task exclusively reads LittleFS files,
 * I2S writing, amplifier control and actual playback status.
 *
 * The length of the playback command queue is 1, the new command overwrites the old command that has not been executed, and the prompt tone queuing is not implemented.
 * playOnce() can temporarily interrupt playLoop(). When a playback ends normally and there is no update command during the period,
 * Automatically restore the latest loop that is still valid. The playback API that returns bool only indicates whether the command is accepted.
 * Does not mean that the sound has started or finished playing.
 */
class SpeakerController {
public:
    SpeakerController();
    ~SpeakerController();

    /*
     * Initialize LittleFS, synchronization object, command queue of length 1 and speaker task.
     * After success, it is in silent mode and uses the default volume; repeated calls remain idempotent.
     */
    bool begin();

    /*
     * Stop receiving new commands, exit the speaker task collaboratively, close I2S and power amplifier, and release resources.
     * Forced cleanup is only performed when the task times out and does not exit; repeated calls will not produce additional effects.
     */
    void end();

    /*
     * Set target software volume, input clamped to SPEAKER_MAX_VOLUME.
     * The volume does not occupy the playback queue, and the current playback will use the new value starting from the next audio chunk.
     */
    bool setVolume(uint8_t volume);

    // Returns the last successfully set and clamped target volume.
    uint8_t volume() const;

    /*
     * Non-blocking delivery and one-time playback. audio_path is a borrowed pointer that must be used during playback and possibly loop
     * Remains valid during recovery, SpeakerController does not copy or release the string.
     *
     * The new playOnce() will replace the old once that has not been completed; if there is a valid latest loop,
     * Restore the loop after the last once ends normally. Acceptance of the command does not mean playback is complete.
     */
    bool playOnce(const char* audio_path);

    /*
     * Non-blocking replacement and starting the latest loop as soon as possible. audio_path uses the same borrowing semantics as playOnce().
     * The new loop will cancel the busy and old recovery intention of the current once; the loop itself is not counted as busy.
     */
    bool playLoop(const char* audio_path);

    /*
     * Non-blocking stops the current sound and synchronously clears the latest loop, resume intent and busy when the command is accepted.
     * stop() does not clear the historical failure status saved by lastPlaybackFailed().
     */
    bool stop();

    // Returns true when the one-time playback has been queued or is being executed; continuous loop is not considered busy.
    bool isBusy() const;

    // Return true when begin() succeeds and has not yet entered end().
    bool isBegun() const;

    // Returns true if the speaker task has been created and has not yet exited.
    bool isTaskRunning() const;

    /*
     * Returns the mode and path actually applied by the speaker task, and does not return unconsumed queued commands.
     * currentAudioPath() returns nullptr after mute or end() completes.
     */
    SpeakerMode currentMode() const;
    const char* currentAudioPath() const;

    /*
     * The latest playback command returns true when a file, WAV or I2S error occurs.
     * This status is cleared when begin() or a new playback command is accepted; stop() and volume update are not cleared.
     */
    bool lastPlaybackFailed() const;

private:
    static constexpr size_t AUDIO_BUFFER_BYTES = 1024;

    enum CommandType : uint8_t {
        COMMAND_STOP = 0,
        COMMAND_PLAY_ONCE,
        COMMAND_PLAY_LOOP
    };

    // The path is a borrowed pointer; sequence is used to prevent expired commands from modifying the latest status.
    struct Command {
        CommandType type = COMMAND_STOP;
        const char* audio_path = nullptr;
        uint32_t sequence = 0;
    };

    // The first version only accepts 16-bit PCM mono or binaural WAV.
    struct WavInfo {
        uint16_t audio_format = 0;
        uint16_t channels = 0;
        uint32_t sample_rate = 0;
        uint32_t byte_rate = 0;
        uint16_t block_align = 0;
        uint16_t bits_per_sample = 0;
        uint32_t data_start = 0;
        uint32_t data_size = 0;
    };

    enum PlaybackResult : uint8_t {
        PLAYBACK_FINISHED = 0,
        PLAYBACK_INTERRUPTED,
        PLAYBACK_FAILED
    };

    I2SClass _i2s;

    // The queue only saves the latest commands in stop/playOnce/playLoop, and the volume does not occupy the queue.
    QueueHandle_t _command_queue = nullptr;

    // The lock sequence is fixed to api -> queue -> state; the speaker task never acquires the api lock.
    SemaphoreHandle_t _api_mutex = nullptr;
    SemaphoreHandle_t _queue_mutex = nullptr;
    SemaphoreHandle_t _task_stopped = nullptr;
    TaskHandle_t _task_handle = nullptr;
    mutable portMUX_TYPE _state_mux = portMUX_INITIALIZER_UNLOCKED;

    // The following cross-task states are uniformly protected by _state_mux.
    SpeakerMode _current_mode = SPEAKER_MODE_SILENT;
    const char* _current_path = nullptr;
    const char* _latest_loop_path = nullptr;
    uint32_t _latest_sequence = 0;
    uint32_t _active_sequence = 0;
    uint8_t _requested_volume = SPEAKER_DEFAULT_VOLUME;
    bool _restore_loop_after_once = false;
    bool _busy = false;
    bool _begun = false;
    bool _task_running = false;
    bool _stopping = false;
    bool _last_playback_failed = false;

    // The following playback and hardware status are exclusively owned by the speaker task; except for forced cleanup by end().
    bool _i2s_started = false;
    bool _speaker_enabled = false;
    bool _fade_in_next_playback = false;
    int16_t _last_left_sample = 0;
    int16_t _last_right_sample = 0;
    uint32_t _fade_in_frames_left = 0;
    uint32_t _fade_in_frames_total = 0;
    uint32_t _i2s_sample_rate = 0;
    uint32_t _last_output_ms = 0;

    // WAV data will be accessed as int16_t, explicitly ensuring that the buffer meets the alignment requirements of the sampling type.
    alignas(int16_t) uint8_t _audio_buffer[AUDIO_BUFFER_BYTES];
    int16_t _stereo_buffer[AUDIO_BUFFER_BYTES];

    static void taskEntry(void* arg);

    // Consume the latest command and execute the current mode; loop playback restarts at the end of the file.
    void taskLoop();

    // Non-blocking submission of playback commands, and synchronous maintenance of sequence, loop recovery intention and busy.
    bool sendPlaybackCommand(CommandType type, const char* audio_path);

    // Speaker task non-blocking read queue; if the queue lock is not obtained, it will be left to the next round of processing.
    bool receiveCommand(Command& command);

    // Only the commands that are still in the latest sequence are applied, and expired commands are directly discarded.
    bool handleCommand(const Command& command);

    // once only ends or resumes the latest loop when the sequence has not changed.
    void finishOnce(uint32_t finished_sequence);

    // If playback fails, only the status of the latest and executing command can be modified.
    void failPlayback(uint32_t failed_sequence);

    bool taskShouldRun() const;
    void setTaskRunning(bool running);
    void setTaskHandle(TaskHandle_t task_handle);

    // Parse and verify RIFF/WAVE, fmt and data block boundaries and PCM format.
    bool readWavInfo(File& file, WavInfo& info);
    bool startI2S(const WavInfo& info);
    void stopI2S();
    void setSpeakerEnabled(bool enabled);
    bool writeSilence(uint32_t sample_rate, uint16_t duration_ms);
    bool writeFadeToSilence(uint32_t sample_rate, uint16_t duration_ms);
    int16_t applyFadeIn(int16_t sample);
    void rememberOutputSample(int16_t left, int16_t right);
    void resetOutputSamples();
    void shutdownAudioOutput();

    /*
     * Play a WAV file and check the latest command before each audio chunk.
     * append_end_silence is only used for fade-out and tail silence at the normal end of once.
     */
    PlaybackResult playWav(const char* audio_path, bool append_end_silence);
    bool writeAudioChunk(uint8_t* data,
                         size_t bytes_read,
                         const WavInfo& info,
                         uint8_t volume);
    int16_t applyVolume(int16_t sample, uint8_t volume) const;
};

#endif
