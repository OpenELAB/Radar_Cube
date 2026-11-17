#include "speaker_controller.h"

#include <limits.h>
#include <string.h>

namespace {

// These parameters only describe audio hardware timing and task operation constraints, and do not carry business policies.
constexpr uint8_t SPEAKER_SHUTDOWN_ACTIVE_LEVEL = LOW;
constexpr uint16_t SPEAKER_ENABLE_SETTLE_MS = 5;
constexpr uint16_t SPEAKER_END_SILENCE_MS = 80;
constexpr uint16_t SPEAKER_SHUTDOWN_SETTLE_MS = 2;
constexpr uint32_t SPEAKER_IDLE_SHUTDOWN_MS = 500;
constexpr uint32_t SPEAKER_TASK_STOP_TIMEOUT_MS = 1000;
constexpr uint32_t SPEAKER_TASK_STACK_BYTES = 8192;
constexpr UBaseType_t SPEAKER_TASK_PRIORITY = 1;
constexpr UBaseType_t SPEAKER_COMMAND_QUEUE_LENGTH = 1;
constexpr uint16_t SPEAKER_TASK_IDLE_DELAY_MS = 10;
constexpr uint16_t SPEAKER_FADE_MS = 30;

bool readExact(File& file, void* destination, size_t length)
{
    return file.read(static_cast<uint8_t*>(destination), length) == length;
}

bool readLE16(File& file, uint16_t& value)
{
    uint8_t bytes[2];
    if (!readExact(file, bytes, sizeof(bytes))) {
        return false;
    }

    value = static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8);
    return true;
}

bool readLE32(File& file, uint32_t& value)
{
    uint8_t bytes[4];
    if (!readExact(file, bytes, sizeof(bytes))) {
        return false;
    }

    value = static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool readChunkId(File& file, char id[5])
{
    if (!readExact(file, id, 4)) {
        return false;
    }

    id[4] = '\0';
    return true;
}

bool durationFrames(uint32_t sample_rate,
                    uint16_t duration_ms,
                    uint32_t& frame_count)
{
    const uint64_t frames =
        (static_cast<uint64_t>(sample_rate) * duration_ms) / 1000U;
    if (frames > UINT32_MAX) {
        return false;
    }

    frame_count = static_cast<uint32_t>(frames);
    if (duration_ms != 0 && frame_count == 0) {
        frame_count = 1;
    }
    return true;
}

} // Anonymous namespace

SpeakerController::SpeakerController() = default;

SpeakerController::~SpeakerController()
{
    end();
}

bool SpeakerController::begin()
{
    if (isBegun()) {
        return true;
    }

    _api_mutex = xSemaphoreCreateMutex();
    if (_api_mutex == nullptr) {
        return false;
    }

    _queue_mutex = xSemaphoreCreateMutex();
    if (_queue_mutex == nullptr) {
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    _command_queue = xQueueCreate(
        SPEAKER_COMMAND_QUEUE_LENGTH,
        sizeof(Command));
    if (_command_queue == nullptr) {
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    _task_stopped = xSemaphoreCreateBinary();
    if (_task_stopped == nullptr) {
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    pinMode(SPEAKER_SHUTDOWN_PIN, OUTPUT);
    digitalWrite(SPEAKER_SHUTDOWN_PIN, SPEAKER_SHUTDOWN_ACTIVE_LEVEL);
    _speaker_enabled = false;

    if (!LittleFS.begin()) {
        vSemaphoreDelete(_task_stopped);
        _task_stopped = nullptr;
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    // Restore the cross-task state at once before task creation to avoid inheriting the playback intention of the previous life cycle.
    taskENTER_CRITICAL(&_state_mux);
    _current_mode = SPEAKER_MODE_SILENT;
    _current_path = nullptr;
    _latest_loop_path = nullptr;
    _latest_sequence = 0;
    _active_sequence = 0;
    _requested_volume = SPEAKER_DEFAULT_VOLUME;
    _restore_loop_after_once = false;
    _busy = false;
    _begun = false;
    _task_running = true;
    _stopping = false;
    _last_playback_failed = false;
    taskEXIT_CRITICAL(&_state_mux);

    _i2s_started = false;
    _speaker_enabled = false;
    _fade_in_next_playback = false;
    _i2s_sample_rate = 0;
    _last_output_ms = 0;
    resetOutputSamples();

    // The task handle is first received by the local variable, and then published to the shared state after the creation is successful.
    TaskHandle_t created_handle = nullptr;
    const BaseType_t created = xTaskCreate(
        taskEntry,
        "speaker",
        SPEAKER_TASK_STACK_BYTES,
        this,
        SPEAKER_TASK_PRIORITY,
        &created_handle);

    if (created != pdPASS) {
        setTaskRunning(false);
        LittleFS.end();
        vSemaphoreDelete(_task_stopped);
        _task_stopped = nullptr;
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    setTaskHandle(created_handle);
    taskENTER_CRITICAL(&_state_mux);
    _begun = true;
    taskEXIT_CRITICAL(&_state_mux);
    return true;
}

void SpeakerController::end()
{
    SemaphoreHandle_t api_mutex = _api_mutex;
    if (api_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(api_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    taskENTER_CRITICAL(&_state_mux);
    const bool has_task = _task_handle != nullptr;
    _begun = false;
    _stopping = true;
    _task_running = false;
    _latest_sequence++;
    _latest_loop_path = nullptr;
    _restore_loop_after_once = false;
    _busy = false;
    SemaphoreHandle_t task_stopped = _task_stopped;
    taskEXIT_CRITICAL(&_state_mux);

    xSemaphoreGive(api_mutex);

    // Prioritize waiting for the task to close the file, I2S and amplifier by itself, and then take over and exit ownership after timeout.
    bool stopped_cleanly = !has_task;
    if (has_task && task_stopped != nullptr) {
        stopped_cleanly = xSemaphoreTake(
            task_stopped,
            pdMS_TO_TICKS(SPEAKER_TASK_STOP_TIMEOUT_MS)) == pdTRUE;
    }

    if (!stopped_cleanly) {
        // Atomic contention with the task to exit ownership. Only the party that obtains the handle is responsible for forcibly deleting the task.
        taskENTER_CRITICAL(&_state_mux);
        TaskHandle_t task_handle = _task_handle;
        _task_handle = nullptr;
        taskEXIT_CRITICAL(&_state_mux);

        if (task_handle != nullptr) {
            vTaskDelete(task_handle);
            setTaskRunning(false);
            shutdownAudioOutput();
        } else if (task_stopped != nullptr) {
            stopped_cleanly = xSemaphoreTake(task_stopped, portMAX_DELAY) == pdTRUE;
        }
    } else {
        setTaskHandle(nullptr);
    }

    if (xSemaphoreTake(api_mutex, portMAX_DELAY) == pdTRUE) {
        LittleFS.end();

        if (_command_queue != nullptr) {
            vQueueDelete(_command_queue);
            _command_queue = nullptr;
        }
        if (_queue_mutex != nullptr) {
            vSemaphoreDelete(_queue_mutex);
            _queue_mutex = nullptr;
        }
        if (_task_stopped != nullptr) {
            vSemaphoreDelete(_task_stopped);
            _task_stopped = nullptr;
        }

        taskENTER_CRITICAL(&_state_mux);
        _current_mode = SPEAKER_MODE_SILENT;
        _current_path = nullptr;
        _latest_loop_path = nullptr;
        _active_sequence = 0;
        _restore_loop_after_once = false;
        _busy = false;
        _stopping = false;
        taskEXIT_CRITICAL(&_state_mux);
        xSemaphoreGive(api_mutex);
    }

    _api_mutex = nullptr;
    vSemaphoreDelete(api_mutex);
}

bool SpeakerController::setVolume(uint8_t volume_value)
{
    const uint8_t clamped = volume_value > SPEAKER_MAX_VOLUME
        ? SPEAKER_MAX_VOLUME
        : volume_value;

    SemaphoreHandle_t api_mutex = _api_mutex;
    if (api_mutex == nullptr || xSemaphoreTake(api_mutex, 0) != pdTRUE) {
        return false;
    }

    taskENTER_CRITICAL(&_state_mux);
    const bool can_set = _begun && !_stopping;
    if (can_set) {
        _requested_volume = clamped;
    }
    taskEXIT_CRITICAL(&_state_mux);

    xSemaphoreGive(api_mutex);
    return can_set;
}

uint8_t SpeakerController::volume() const
{
    taskENTER_CRITICAL(&_state_mux);
    const uint8_t current_volume = _requested_volume;
    taskEXIT_CRITICAL(&_state_mux);
    return current_volume;
}

bool SpeakerController::playOnce(const char* audio_path)
{
    if (audio_path == nullptr || audio_path[0] == '\0') {
        return false;
    }

    return sendPlaybackCommand(COMMAND_PLAY_ONCE, audio_path);
}

bool SpeakerController::playLoop(const char* audio_path)
{
    if (audio_path == nullptr || audio_path[0] == '\0') {
        return false;
    }

    return sendPlaybackCommand(COMMAND_PLAY_LOOP, audio_path);
}

bool SpeakerController::stop()
{
    return sendPlaybackCommand(COMMAND_STOP, nullptr);
}

bool SpeakerController::isBusy() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool busy = _busy;
    taskEXIT_CRITICAL(&_state_mux);
    return busy;
}

bool SpeakerController::isBegun() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool begun = _begun;
    taskEXIT_CRITICAL(&_state_mux);
    return begun;
}

bool SpeakerController::isTaskRunning() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool running = _task_running && _task_handle != nullptr;
    taskEXIT_CRITICAL(&_state_mux);
    return running;
}

SpeakerMode SpeakerController::currentMode() const
{
    taskENTER_CRITICAL(&_state_mux);
    const SpeakerMode mode = _current_mode;
    taskEXIT_CRITICAL(&_state_mux);
    return mode;
}

const char* SpeakerController::currentAudioPath() const
{
    taskENTER_CRITICAL(&_state_mux);
    const char* path = _current_path;
    taskEXIT_CRITICAL(&_state_mux);
    return path;
}

bool SpeakerController::lastPlaybackFailed() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool failed = _last_playback_failed;
    taskEXIT_CRITICAL(&_state_mux);
    return failed;
}

void SpeakerController::taskEntry(void* arg)
{
    SpeakerController* controller = static_cast<SpeakerController*>(arg);
    if (controller == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    controller->taskLoop();
}

void SpeakerController::taskLoop()
{
    while (taskShouldRun()) {
        Command command;
        if (receiveCommand(command)) {
            handleCommand(command);
        }

        taskENTER_CRITICAL(&_state_mux);
        const SpeakerMode mode = _current_mode;
        const char* path = _current_path;
        const uint32_t sequence = _active_sequence;
        taskEXIT_CRITICAL(&_state_mux);

        switch (mode) {
        case SPEAKER_MODE_SILENT:
            if (_i2s_started &&
                _last_output_ms != 0 &&
                millis() - _last_output_ms >= SPEAKER_IDLE_SHUTDOWN_MS) {
                shutdownAudioOutput();
            }
            break;

        case SPEAKER_MODE_PLAY_ONCE:
            {
                const PlaybackResult result = playWav(path, true);
                if (result == PLAYBACK_FINISHED) {
                    finishOnce(sequence);
                } else if (result == PLAYBACK_FAILED) {
                    failPlayback(sequence);
                }
            }
            break;

        case SPEAKER_MODE_LOOP:
            {
                const PlaybackResult result = playWav(path, false);
                if (result == PLAYBACK_FAILED) {
                    failPlayback(sequence);
                }
            }
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
    }

    shutdownAudioOutput();

    taskENTER_CRITICAL(&_state_mux);
    _current_mode = SPEAKER_MODE_SILENT;
    _current_path = nullptr;
    _busy = false;
    taskEXIT_CRITICAL(&_state_mux);
    setTaskRunning(false);

    // The task first obtains exit ownership and then notifies end(); if end() has taken over, it waits to be forcibly deleted.
    taskENTER_CRITICAL(&_state_mux);
    const bool owns_task_exit = _task_handle != nullptr;
    if (owns_task_exit) {
        _task_handle = nullptr;
    }
    taskEXIT_CRITICAL(&_state_mux);

    if (owns_task_exit) {
        SemaphoreHandle_t task_stopped = _task_stopped;
        if (task_stopped != nullptr) {
            xSemaphoreGive(task_stopped);
        }
        vTaskDelete(nullptr);
    }

    vTaskSuspend(nullptr);
}

bool SpeakerController::sendPlaybackCommand(CommandType type, const char* audio_path)
{
    SemaphoreHandle_t api_mutex = _api_mutex;
    if (api_mutex == nullptr || xSemaphoreTake(api_mutex, 0) != pdTRUE) {
        return false;
    }

    taskENTER_CRITICAL(&_state_mux);
    const bool can_send = _begun && !_stopping;
    taskEXIT_CRITICAL(&_state_mux);
    if (!can_send || _command_queue == nullptr || _queue_mutex == nullptr) {
        xSemaphoreGive(api_mutex);
        return false;
    }

    // The playback API must be non-blocking; when the queue lock is busy, the caller decides whether to retry.
    if (xSemaphoreTake(_queue_mutex, 0) != pdTRUE) {
        xSemaphoreGive(api_mutex);
        return false;
    }

    Command command;
    command.type = type;
    command.audio_path = audio_path;

    /*
     * Synchronously update the "latest request" status before the command enters the queue, so that the semantics of isBusy() and stop() change from
     * It takes effect immediately when the API returns. If the overwriting fails unexpectedly, press sequence below to roll back this update.
     */
    taskENTER_CRITICAL(&_state_mux);
    const uint32_t previous_sequence = _latest_sequence;
    const char* previous_loop_path = _latest_loop_path;
    const bool previous_restore = _restore_loop_after_once;
    const bool previous_busy = _busy;
    const bool previous_failed = _last_playback_failed;

    command.sequence = _latest_sequence + 1U;
    _latest_sequence = command.sequence;

    switch (type) {
    case COMMAND_STOP:
        _latest_loop_path = nullptr;
        _restore_loop_after_once = false;
        _busy = false;
        break;

    case COMMAND_PLAY_ONCE:
        _restore_loop_after_once = _latest_loop_path != nullptr;
        _busy = true;
        _last_playback_failed = false;
        break;

    case COMMAND_PLAY_LOOP:
        _latest_loop_path = audio_path;
        _restore_loop_after_once = false;
        _busy = false;
        _last_playback_failed = false;
        break;
    }
    taskEXIT_CRITICAL(&_state_mux);

    const bool sent = xQueueOverwrite(_command_queue, &command) == pdPASS;
    if (!sent) {
        taskENTER_CRITICAL(&_state_mux);
        if (_latest_sequence == command.sequence) {
            _latest_sequence = previous_sequence;
            _latest_loop_path = previous_loop_path;
            _restore_loop_after_once = previous_restore;
            _busy = previous_busy;
            _last_playback_failed = previous_failed;
        }
        taskEXIT_CRITICAL(&_state_mux);
    }

    xSemaphoreGive(_queue_mutex);
    xSemaphoreGive(api_mutex);
    return sent;
}

bool SpeakerController::receiveCommand(Command& command)
{
    SemaphoreHandle_t queue_mutex = _queue_mutex;
    // The task does not wait for the API to release the queue lock, and will check again in the next round or next audio chunk.
    if (queue_mutex == nullptr || xSemaphoreTake(queue_mutex, 0) != pdTRUE) {
        return false;
    }

    const bool received = _command_queue != nullptr &&
        xQueueReceive(_command_queue, &command, 0) == pdTRUE;
    xSemaphoreGive(queue_mutex);
    return received;
}

bool SpeakerController::handleCommand(const Command& command)
{
    taskENTER_CRITICAL(&_state_mux);
    // Another update command may arrive after the queue is read, and the expired sequence cannot overwrite the latest status.
    if (command.sequence != _latest_sequence) {
        taskEXIT_CRITICAL(&_state_mux);
        return false;
    }

    _active_sequence = command.sequence;
    switch (command.type) {
    case COMMAND_STOP:
        _current_mode = SPEAKER_MODE_SILENT;
        _current_path = nullptr;
        break;

    case COMMAND_PLAY_ONCE:
        _current_mode = SPEAKER_MODE_PLAY_ONCE;
        _current_path = command.audio_path;
        break;

    case COMMAND_PLAY_LOOP:
        _current_mode = SPEAKER_MODE_LOOP;
        _current_path = command.audio_path;
        break;
    }
    taskEXIT_CRITICAL(&_state_mux);
    return true;
}

void SpeakerController::finishOnce(uint32_t finished_sequence)
{
    taskENTER_CRITICAL(&_state_mux);
    // As long as an update command occurs during playback, the old once cannot end the busy or resume the old loop.
    if (_latest_sequence != finished_sequence ||
        _active_sequence != finished_sequence) {
        taskEXIT_CRITICAL(&_state_mux);
        return;
    }

    if (_restore_loop_after_once && _latest_loop_path != nullptr) {
        _current_mode = SPEAKER_MODE_LOOP;
        _current_path = _latest_loop_path;
    } else {
        _current_mode = SPEAKER_MODE_SILENT;
        _current_path = nullptr;
    }

    _restore_loop_after_once = false;
    _busy = false;
    taskEXIT_CRITICAL(&_state_mux);
}

void SpeakerController::failPlayback(uint32_t failed_sequence)
{
    taskENTER_CRITICAL(&_state_mux);
    // Failure of expired playback cannot cause new commands arriving subsequently to switch back to mute.
    if (_latest_sequence == failed_sequence &&
        _active_sequence == failed_sequence) {
        _current_mode = SPEAKER_MODE_SILENT;
        _current_path = nullptr;
        _latest_loop_path = nullptr;
        _restore_loop_after_once = false;
        _busy = false;
        _last_playback_failed = true;
    }
    taskEXIT_CRITICAL(&_state_mux);
}

bool SpeakerController::taskShouldRun() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool running = _task_running;
    taskEXIT_CRITICAL(&_state_mux);
    return running;
}

void SpeakerController::setTaskRunning(bool running)
{
    taskENTER_CRITICAL(&_state_mux);
    _task_running = running;
    taskEXIT_CRITICAL(&_state_mux);
}

void SpeakerController::setTaskHandle(TaskHandle_t task_handle)
{
    taskENTER_CRITICAL(&_state_mux);
    _task_handle = task_handle;
    taskEXIT_CRITICAL(&_state_mux);
}

bool SpeakerController::readWavInfo(File& file, WavInfo& info)
{
    info = WavInfo{};

    const size_t raw_file_size = file.size();
    if (raw_file_size < 12 || raw_file_size > UINT32_MAX) {
        return false;
    }
    const uint32_t file_size = static_cast<uint32_t>(raw_file_size);

    char id[5];
    uint32_t riff_size = 0;
    if (!readChunkId(file, id) || strcmp(id, "RIFF") != 0 ||
        !readLE32(file, riff_size) ||
        !readChunkId(file, id) || strcmp(id, "WAVE") != 0) {
        return false;
    }

    if (riff_size < 4 || riff_size > file_size - 8U) {
        return false;
    }
    const uint32_t riff_end = riff_size + 8U;

    bool found_fmt = false;
    bool found_data = false;

    // Parse block-by-block within the RIFF declaration range, rejecting out-of-bounds, truncated, and illegal padding bytes.
    while (file.position() <= riff_end - 8U) {
        uint32_t chunk_size = 0;
        if (!readChunkId(file, id) || !readLE32(file, chunk_size)) {
            return false;
        }

        const uint32_t chunk_data_start = file.position();
        if (chunk_data_start > riff_end ||
            chunk_size > riff_end - chunk_data_start) {
            return false;
        }
        const uint32_t chunk_end = chunk_data_start + chunk_size;

        if (strcmp(id, "fmt ") == 0) {
            if (chunk_size < 16 ||
                !readLE16(file, info.audio_format) ||
                !readLE16(file, info.channels) ||
                !readLE32(file, info.sample_rate) ||
                !readLE32(file, info.byte_rate) ||
                !readLE16(file, info.block_align) ||
                !readLE16(file, info.bits_per_sample)) {
                return false;
            }
            found_fmt = true;
        } else if (strcmp(id, "data") == 0) {
            info.data_start = chunk_data_start;
            info.data_size = chunk_size;
            found_data = true;
            break;
        }

        const uint32_t padding = chunk_size & 1U;
        if (padding > riff_end - chunk_end) {
            return false;
        }
        const uint32_t padded_end = chunk_end + padding;
        if (!file.seek(padded_end)) {
            return false;
        }
    }

    const uint16_t expected_block_align = static_cast<uint16_t>(
        info.channels * sizeof(int16_t));
    const uint64_t expected_byte_rate =
        static_cast<uint64_t>(info.sample_rate) * expected_block_align;

    return found_fmt && found_data &&
        info.audio_format == 1 &&
        info.sample_rate != 0 &&
        (info.channels == 1 || info.channels == 2) &&
        info.bits_per_sample == 16 &&
        info.block_align == expected_block_align &&
        info.byte_rate == expected_byte_rate &&
        info.data_size != 0 &&
        info.data_size % info.block_align == 0;
}

bool SpeakerController::startI2S(const WavInfo& info)
{
    // Reuse the existing I2S when the sampling rate does not change to avoid repeated hardware initialization in each loop cycle.
    if (_i2s_started && _i2s_sample_rate == info.sample_rate) {
        setSpeakerEnabled(true);
        return true;
    }

    shutdownAudioOutput();

    _i2s.setPins(SPEAKER_I2S_BCLK_PIN, SPEAKER_I2S_LRC_PIN, SPEAKER_I2S_DIN_PIN);
    _i2s_started = _i2s.begin(
        I2S_MODE_STD,
        info.sample_rate,
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO);

    if (!_i2s_started) {
        _i2s_sample_rate = 0;
        return false;
    }

    _i2s_sample_rate = info.sample_rate;
    setSpeakerEnabled(true);
    delay(SPEAKER_ENABLE_SETTLE_MS);
    return true;
}

void SpeakerController::stopI2S()
{
    if (!_i2s_started) {
        return;
    }

    _i2s.end();
    _i2s_started = false;
    _i2s_sample_rate = 0;
    resetOutputSamples();
}

void SpeakerController::setSpeakerEnabled(bool enabled)
{
    if (_speaker_enabled == enabled) {
        return;
    }

    const uint8_t inactive_level =
        SPEAKER_SHUTDOWN_ACTIVE_LEVEL == LOW ? HIGH : LOW;
    digitalWrite(
        SPEAKER_SHUTDOWN_PIN,
        enabled ? inactive_level : SPEAKER_SHUTDOWN_ACTIVE_LEVEL);
    _speaker_enabled = enabled;
}

bool SpeakerController::writeSilence(uint32_t sample_rate, uint16_t duration_ms)
{
    if (!_i2s_started) {
        return false;
    }
    if (duration_ms == 0) {
        return true;
    }

    memset(_audio_buffer, 0, sizeof(_audio_buffer));

    uint32_t frame_count = 0;
    if (!durationFrames(sample_rate, duration_ms, frame_count)) {
        return false;
    }

    // Calculate by frame first and then convert bytes to ensure that the two-channel frame alignment is maintained for each tail silence.
    uint64_t bytes_left =
        static_cast<uint64_t>(frame_count) * 2U * sizeof(int16_t);
    while (bytes_left > 0) {
        const size_t to_write = static_cast<size_t>(
            bytes_left < sizeof(_audio_buffer)
                ? bytes_left
                : sizeof(_audio_buffer));
        if (_i2s.write(_audio_buffer, to_write) != to_write) {
            return false;
        }
        bytes_left -= to_write;
    }

    resetOutputSamples();
    return true;
}

bool SpeakerController::writeFadeToSilence(uint32_t sample_rate,
                                           uint16_t duration_ms)
{
    if (!_i2s_started || sample_rate == 0) {
        return false;
    }
    if (duration_ms == 0) {
        return true;
    }

    uint32_t total_frames = 0;
    if (!durationFrames(sample_rate, duration_ms, total_frames)) {
        return false;
    }

    uint32_t frame_index = 0;
    const size_t max_frames_per_write =
        sizeof(_stereo_buffer) / (2U * sizeof(int16_t));

    while (frame_index < total_frames) {
        const uint32_t frames_left = total_frames - frame_index;
        const size_t frames_this_write = static_cast<size_t>(
            frames_left < max_frames_per_write
                ? frames_left
                : max_frames_per_write);

        for (size_t i = 0; i < frames_this_write; i++) {
            const uint32_t frames_remaining = total_frames - frame_index - 1U;
            const int16_t left = static_cast<int16_t>(
                (static_cast<int64_t>(_last_left_sample) * frames_remaining) /
                total_frames);
            const int16_t right = static_cast<int16_t>(
                (static_cast<int64_t>(_last_right_sample) * frames_remaining) /
                total_frames);

            _stereo_buffer[i * 2] = left;
            _stereo_buffer[i * 2 + 1] = right;
            frame_index++;
        }

        const size_t bytes_to_write =
            frames_this_write * 2U * sizeof(int16_t);
        if (_i2s.write(
                reinterpret_cast<const uint8_t*>(_stereo_buffer),
                bytes_to_write) != bytes_to_write) {
            return false;
        }
    }

    // A fade-out has ended the current playback, and the unfinished fade-in progress cannot be carried to the next command.
    resetOutputSamples();
    return true;
}

int16_t SpeakerController::applyFadeIn(int16_t sample)
{
    if (_fade_in_frames_left == 0 || _fade_in_frames_total == 0) {
        return sample;
    }

    const uint32_t frames_done =
        _fade_in_frames_total - _fade_in_frames_left + 1U;
    const int16_t faded = static_cast<int16_t>(
        (static_cast<int64_t>(sample) * frames_done) /
        _fade_in_frames_total);

    _fade_in_frames_left--;
    if (_fade_in_frames_left == 0) {
        _fade_in_frames_total = 0;
    }
    return faded;
}

void SpeakerController::rememberOutputSample(int16_t left, int16_t right)
{
    _last_left_sample = left;
    _last_right_sample = right;
}

void SpeakerController::resetOutputSamples()
{
    _last_left_sample = 0;
    _last_right_sample = 0;
    _fade_in_frames_left = 0;
    _fade_in_frames_total = 0;
}

void SpeakerController::shutdownAudioOutput()
{
    if (_i2s_started && _i2s_sample_rate != 0) {
        (void)writeFadeToSilence(_i2s_sample_rate, SPEAKER_FADE_MS);
        (void)writeSilence(_i2s_sample_rate, SPEAKER_END_SILENCE_MS);
    }

    setSpeakerEnabled(false);
    delay(SPEAKER_SHUTDOWN_SETTLE_MS);
    stopI2S();
}

SpeakerController::PlaybackResult SpeakerController::playWav(
    const char* audio_path,
    bool append_end_silence)
{
    if (audio_path == nullptr || audio_path[0] == '\0') {
        shutdownAudioOutput();
        return PLAYBACK_FAILED;
    }

    File file = LittleFS.open(audio_path, "r");
    if (!file) {
        Serial.print("Could not open audio file: ");
        Serial.println(audio_path);
        shutdownAudioOutput();
        return PLAYBACK_FAILED;
    }

    WavInfo wav_info;
    if (!readWavInfo(file, wav_info)) {
        Serial.print("Unsupported or invalid WAV: ");
        Serial.println(audio_path);
        file.close();
        shutdownAudioOutput();
        return PLAYBACK_FAILED;
    }

    if (!startI2S(wav_info)) {
        Serial.println("I2S init failed");
        file.close();
        shutdownAudioOutput();
        return PLAYBACK_FAILED;
    }

    if (_fade_in_next_playback) {
        if (!durationFrames(
                wav_info.sample_rate,
                SPEAKER_FADE_MS,
                _fade_in_frames_total)) {
            file.close();
            shutdownAudioOutput();
            return PLAYBACK_FAILED;
        }
        _fade_in_frames_left = _fade_in_frames_total;
        _fade_in_next_playback = false;
    }

    if (!file.seek(wav_info.data_start)) {
        file.close();
        shutdownAudioOutput();
        return PLAYBACK_FAILED;
    }

    uint32_t bytes_left = wav_info.data_size;
    while (taskShouldRun() && bytes_left > 0) {
        // Consume the latest command before each chunk to ensure that stop/once/loop can seize the current file in time.
        Command new_command;
        if (receiveCommand(new_command) && handleCommand(new_command)) {
            file.close();

            // Fade out the current sample first; retain the fade-in intention when switching to a new sound, and stop to add tail silence.
            const SpeakerMode next_mode = currentMode();
            const bool faded =
                writeFadeToSilence(wav_info.sample_rate, SPEAKER_FADE_MS);
            if (next_mode == SPEAKER_MODE_SILENT) {
                if (!faded ||
                    !writeSilence(
                        wav_info.sample_rate,
                        SPEAKER_END_SILENCE_MS)) {
                    shutdownAudioOutput();
                }
                _last_output_ms = millis();
            } else {
                if (faded) {
                    _fade_in_next_playback = true;
                } else {
                    shutdownAudioOutput();
                    _fade_in_next_playback = true;
                }
            }
            return PLAYBACK_INTERRUPTED;
        }

        const size_t to_read = min(
            static_cast<uint32_t>(sizeof(_audio_buffer)),
            bytes_left);
        const size_t bytes_read = file.read(_audio_buffer, to_read);
        if (bytes_read != to_read) {
            Serial.print("Audio read ended early: ");
            Serial.println(audio_path);
            file.close();
            shutdownAudioOutput();
            return PLAYBACK_FAILED;
        }

        const uint8_t chunk_volume = volume();
        if (!writeAudioChunk(
                _audio_buffer,
                bytes_read,
                wav_info,
                chunk_volume)) {
            Serial.println("I2S write failed");
            file.close();
            shutdownAudioOutput();
            return PLAYBACK_FAILED;
        }
        bytes_left -= bytes_read;
    }

    if (!taskShouldRun() && bytes_left > 0) {
        file.close();
        return PLAYBACK_INTERRUPTED;
    }

    file.close();
    if (append_end_silence &&
        (!writeFadeToSilence(wav_info.sample_rate, SPEAKER_FADE_MS) ||
         !writeSilence(wav_info.sample_rate, SPEAKER_END_SILENCE_MS))) {
        shutdownAudioOutput();
        return PLAYBACK_FAILED;
    }

    _last_output_ms = millis();
    return PLAYBACK_FINISHED;
}

bool SpeakerController::writeAudioChunk(uint8_t* data,
                                        size_t bytes_read,
                                        const WavInfo& info,
                                        uint8_t volume_value)
{
    if (info.channels == 2) {
        // Binaural scaling in place, maintaining the left and right channel arrangement of the input frame.
        int16_t* samples = reinterpret_cast<int16_t*>(data);
        const size_t sample_count = bytes_read / sizeof(int16_t);
        const size_t frame_count = sample_count / 2U;

        for (size_t i = 0; i < frame_count; i++) {
            int16_t left = applyVolume(samples[i * 2], volume_value);
            int16_t right = applyVolume(samples[i * 2 + 1], volume_value);

            if (_fade_in_frames_left != 0 && _fade_in_frames_total != 0) {
                const uint32_t frames_done =
                    _fade_in_frames_total - _fade_in_frames_left + 1U;
                left = static_cast<int16_t>(
                    (static_cast<int64_t>(left) * frames_done) /
                    _fade_in_frames_total);
                right = static_cast<int16_t>(
                    (static_cast<int64_t>(right) * frames_done) /
                    _fade_in_frames_total);
                _fade_in_frames_left--;
                if (_fade_in_frames_left == 0) {
                    _fade_in_frames_total = 0;
                }
            }

            samples[i * 2] = left;
            samples[i * 2 + 1] = right;
            rememberOutputSample(left, right);
        }

        return _i2s.write(data, bytes_read) == bytes_read;
    }

    // The mono channel first applies volume and fade-in, and then expands to the same two-channel frame on the left and right.
    const size_t sample_count = bytes_read / sizeof(int16_t);
    const int16_t* mono_samples = reinterpret_cast<const int16_t*>(data);

    for (size_t i = 0; i < sample_count; i++) {
        int16_t sample = applyVolume(mono_samples[i], volume_value);
        sample = applyFadeIn(sample);
        _stereo_buffer[i * 2] = sample;
        _stereo_buffer[i * 2 + 1] = sample;
        rememberOutputSample(sample, sample);
    }

    const size_t output_bytes = sample_count * 2U * sizeof(int16_t);
    return _i2s.write(
        reinterpret_cast<const uint8_t*>(_stereo_buffer),
        output_bytes) == output_bytes;
}

int16_t SpeakerController::applyVolume(int16_t sample, uint8_t volume_value) const
{
    const int32_t scaled =
        (static_cast<int32_t>(sample) * volume_value) / 100;

    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(scaled);
}
