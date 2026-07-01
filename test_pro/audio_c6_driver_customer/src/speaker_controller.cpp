#include "speaker_controller.h"

#include <string.h>

namespace {

uint16_t readLE16(File& file)
{
    uint8_t bytes[2];
    if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
        return 0;
    }
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readLE32(File& file)
{
    uint8_t bytes[4];
    if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
        return 0;
    }
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

bool readChunkId(File& file, char id[5])
{
    if (file.read(reinterpret_cast<uint8_t*>(id), 4) != 4) {
        return false;
    }
    id[4] = '\0';
    return true;
}

} // namespace

SpeakerController::SpeakerController()
{
    _current_command.mode = SpeakerMode::Silent;
    _current_command.audio = AudioId::None;
    _current_command.volume = SpeakerVolumeLevel::Default;
}

SpeakerController::~SpeakerController()
{
    end();
}

bool SpeakerController::begin()
{
    if (_begun) {
        return true;
    }

    pinMode(SPEAKER_SHUTDOWN_PIN, OUTPUT);
    digitalWrite(SPEAKER_SHUTDOWN_PIN, SPEAKER_SHUTDOWN_ACTIVE_LEVEL);
    _speaker_enabled = false;

    if (!LittleFS.begin()) {
        return false;
    }

    _command_queue = xQueueCreate(SPEAKER_COMMAND_QUEUE_LENGTH, sizeof(SpeakerCommand));
    if (_command_queue == nullptr) {
        LittleFS.end();
        return false;
    }

    _task_running = true;
    _last_output_ms = 0;

    const BaseType_t created = xTaskCreate(
        taskEntry,
        "speaker",
        SPEAKER_TASK_STACK_WORDS,
        this,
        SPEAKER_TASK_PRIORITY,
        &_task_handle);

    if (created != pdPASS) {
        _task_running = false;
        _task_handle = nullptr;
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        LittleFS.end();
        return false;
    }

    _begun = true;
    return true;
}

void SpeakerController::end()
{
    if (!_begun) {
        return;
    }

    stop();

    const uint32_t stop_wait_start = millis();
    while (_current_mode != SpeakerMode::Silent &&
        millis() - stop_wait_start < SPEAKER_IDLE_SHUTDOWN_MS) {
        vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
    }

    _task_running = false;

    const uint32_t wait_start = millis();
    while (_task_handle != nullptr && millis() - wait_start < SPEAKER_IDLE_SHUTDOWN_MS) {
        vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
    }

    if (_task_handle != nullptr) {
        Serial.println("Speaker task did not stop in time; deleting task");
        vTaskDelete(_task_handle);
        _task_handle = nullptr;
    }

    shutdownAudioOutput();

    if (_command_queue != nullptr) {
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
    }

    _current_mode = SpeakerMode::Silent;
    _current_audio = AudioId::None;
    _begun = false;
    LittleFS.end();
}

bool SpeakerController::setVolume(SpeakerVolumeLevel volume)
{
    if (volumeValue(volume) > SPEAKER_MAX_VOLUME) {
        return false;
    }

    _current_volume = volume;
    return true;
}

SpeakerVolumeLevel SpeakerController::volume() const
{
    return _current_volume;
}

bool SpeakerController::stop()
{
    SpeakerCommand command;
    command.mode = SpeakerMode::Silent;
    command.audio = AudioId::None;
    return sendCommand(command);
}

bool SpeakerController::playOnce(AudioId audio)
{
    if (audio == AudioId::None) {
        return stop();
    }

    SpeakerCommand command;
    command.mode = SpeakerMode::PlayOnce;
    command.audio = audio;
    return sendCommand(command);
}

bool SpeakerController::playLoop(AudioId audio)
{
    if (audio == AudioId::None) {
        return stop();
    }

    SpeakerCommand command;
    command.mode = SpeakerMode::Loop;
    command.audio = audio;
    return sendCommand(command);
}

bool SpeakerController::isBegun() const
{
    return _begun;
}

bool SpeakerController::isTaskRunning() const
{
    return _task_running && _task_handle != nullptr;
}

SpeakerMode SpeakerController::currentMode() const
{
    return _current_mode;
}

AudioId SpeakerController::currentAudio() const
{
    return _current_audio;
}

bool SpeakerController::lastPlaybackFailed() const
{
    return _last_playback_failed;
}

void SpeakerController::taskEntry(void* arg)
{
    auto* controller = static_cast<SpeakerController*>(arg);
    controller->taskLoop();
    controller->_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void SpeakerController::taskLoop()
{
    while (_task_running) {
        SpeakerCommand new_command;
        if (xQueueReceive(_command_queue, &new_command, 0) == pdTRUE) {
            _current_command = new_command;
            _current_mode = new_command.mode;
            _current_audio = new_command.audio;
            if (_current_mode != SpeakerMode::Silent) {
                _last_playback_failed = false;
            }
        }

        switch (_current_mode) {
        case SpeakerMode::Silent:
            _current_audio = AudioId::None;
            if (_i2s_started &&
                _last_output_ms != 0 &&
                millis() - _last_output_ms >= SPEAKER_IDLE_SHUTDOWN_MS) {
                shutdownAudioOutput();
            }
            break;

        case SpeakerMode::PlayOnce:
            {
                const PlaybackResult result = playWav(_current_audio, true);
                if (result != PlaybackResult::Interrupted) {
                    _last_playback_failed = (result == PlaybackResult::Failed);
                    _current_command.mode = SpeakerMode::Silent;
                    _current_command.audio = AudioId::None;
                    _current_mode = SpeakerMode::Silent;
                    _current_audio = AudioId::None;
                }
            }
            break;

        case SpeakerMode::Loop:
            {
                const PlaybackResult result = playWav(_current_audio, false);
                if (result == PlaybackResult::Failed) {
                    _last_playback_failed = true;
                    _current_command.mode = SpeakerMode::Silent;
                    _current_command.audio = AudioId::None;
                    _current_mode = SpeakerMode::Silent;
                    _current_audio = AudioId::None;
                } else if (result == PlaybackResult::Finished) {
                    _last_playback_failed = false;
                }
            }
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
    }

    if (_i2s_started) {
        shutdownAudioOutput();
    }
}

bool SpeakerController::sendCommand(const SpeakerCommand& command)
{
    if (!_begun || _command_queue == nullptr) {
        return false;
    }

    return xQueueOverwrite(_command_queue, &command) == pdPASS;
}

const char* SpeakerController::audioFilePath(AudioId audio) const
{
    switch (audio) {
    case AudioId::BeepSlow:
        return SPEAKER_FILE_BEEP_SLOW;
    case AudioId::BeepMediumSlow:
        return SPEAKER_FILE_BEEP_MEDIUM_SLOW;
    case AudioId::BeepMedium:
        return SPEAKER_FILE_BEEP_MEDIUM;
    case AudioId::BeepFast:
        return SPEAKER_FILE_BEEP_FAST;
    case AudioId::BeepContinuous:
        return SPEAKER_FILE_BEEP_CONTINUOUS;
    case AudioId::Boot:
        return SPEAKER_FILE_BOOT;
    case AudioId::PairOk:
        return SPEAKER_FILE_PAIR_OK;
    case AudioId::PairFail:
        return SPEAKER_FILE_PAIR_FAIL;
    case AudioId::ConnectionLost:
        return SPEAKER_FILE_CONNECTION_LOST;
    case AudioId::LowBattery:
        return SPEAKER_FILE_LOW_BATTERY;
    case AudioId::Fault:
        return SPEAKER_FILE_FAULT;
    case AudioId::None:
    default:
        return nullptr;
    }
}

uint8_t SpeakerController::volumeValue(SpeakerVolumeLevel volume) const
{
    switch (volume) {
    case SpeakerVolumeLevel::Low:
        return SPEAKER_VOLUME_LOW_VALUE;
    case SpeakerVolumeLevel::Medium:
        return SPEAKER_VOLUME_MED_VALUE;
    case SpeakerVolumeLevel::High:
        return SPEAKER_VOLUME_HIGH_VALUE;
    case SpeakerVolumeLevel::Default:
    default:
        return SPEAKER_DEFAULT_VOLUME;
    }
}

bool SpeakerController::readWavInfo(File& file, WavInfo& info)
{
    char id[5];

    if (!readChunkId(file, id) || strcmp(id, "RIFF") != 0) {
        return false;
    }

    readLE32(file);

    if (!readChunkId(file, id) || strcmp(id, "WAVE") != 0) {
        return false;
    }

    bool found_fmt = false;
    bool found_data = false;

    while (file.available()) {
        if (!readChunkId(file, id)) {
            break;
        }

        const uint32_t chunk_size = readLE32(file);
        const uint32_t chunk_data_start = file.position();

        if (strcmp(id, "fmt ") == 0) {
            info.audio_format = readLE16(file);
            info.channels = readLE16(file);
            info.sample_rate = readLE32(file);
            readLE32(file);
            readLE16(file);
            info.bits_per_sample = readLE16(file);
            found_fmt = true;
        } else if (strcmp(id, "data") == 0) {
            info.data_start = chunk_data_start;
            info.data_size = chunk_size;
            found_data = true;
            break;
        }

        file.seek(chunk_data_start + chunk_size + (chunk_size & 1));
    }

    return found_fmt && found_data &&
        info.audio_format == 1 &&
        info.bits_per_sample == 16 &&
        (info.channels == 1 || info.channels == 2);
}

bool SpeakerController::startI2S(const WavInfo& info)
{
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

    if (_i2s_started) {
        _i2s_sample_rate = info.sample_rate;
        setSpeakerEnabled(true);
        delay(SPEAKER_ENABLE_SETTLE_MS);
    } else {
        _i2s_sample_rate = 0;
    }

    return _i2s_started;
}

void SpeakerController::stopI2S()
{
    if (!_i2s_started) {
        return;
    }

    _i2s.end();
    _i2s_started = false;
    _i2s_sample_rate = 0;
}

void SpeakerController::setSpeakerEnabled(bool enabled)
{
    if (_speaker_enabled == enabled) {
        return;
    }

    const uint8_t inactive_level = (SPEAKER_SHUTDOWN_ACTIVE_LEVEL == LOW) ? HIGH : LOW;
    digitalWrite(SPEAKER_SHUTDOWN_PIN, enabled ? inactive_level : SPEAKER_SHUTDOWN_ACTIVE_LEVEL);
    _speaker_enabled = enabled;
}

void SpeakerController::writeSilence(uint32_t sample_rate, uint16_t duration_ms)
{
    if (!_i2s_started || duration_ms == 0) {
        return;
    }

    memset(_audio_buffer, 0, sizeof(_audio_buffer));

    uint32_t bytes_left = (sample_rate * 2U * sizeof(int16_t) * duration_ms) / 1000U;
    while (bytes_left > 0) {
        const size_t to_write = min(static_cast<uint32_t>(sizeof(_audio_buffer)), bytes_left);
        _i2s.write(_audio_buffer, to_write);
        bytes_left -= to_write;
    }
}

void SpeakerController::shutdownAudioOutput()
{
    if (_i2s_started && _i2s_sample_rate != 0) {
        writeSilence(_i2s_sample_rate, SPEAKER_END_SILENCE_MS);
    }

    setSpeakerEnabled(false);
    delay(SPEAKER_SHUTDOWN_SETTLE_MS);
    stopI2S();
}

SpeakerController::PlaybackResult SpeakerController::playWav(AudioId audio, bool append_end_silence)
{
    const char* audio_file = audioFilePath(audio);
    if (audio_file == nullptr) {
        shutdownAudioOutput();
        return PlaybackResult::Failed;
    }

    File file = LittleFS.open(audio_file, "r");
    if (!file) {
        Serial.print("Could not open audio file: ");
        Serial.println(audio_file);
        shutdownAudioOutput();
        return PlaybackResult::Failed;
    }

    WavInfo wav_info;
    if (!readWavInfo(file, wav_info)) {
        Serial.print("Unsupported WAV: ");
        Serial.println(audio_file);
        file.close();
        shutdownAudioOutput();
        return PlaybackResult::Failed;
    }

    if (!startI2S(wav_info)) {
        Serial.println("I2S init failed");
        file.close();
        shutdownAudioOutput();
        return PlaybackResult::Failed;
    }

    file.seek(wav_info.data_start);
    uint32_t bytes_left = wav_info.data_size;

    while (_task_running && bytes_left > 0) {
        SpeakerCommand new_command;
        if (xQueueReceive(_command_queue, &new_command, 0) == pdTRUE) {
            _current_command = new_command;
            _current_mode = new_command.mode;
            _current_audio = new_command.audio;
            file.close();
            if (_current_mode == SpeakerMode::Silent) {
                shutdownAudioOutput();
            }
            return PlaybackResult::Interrupted;
        }

        const size_t to_read = min(static_cast<uint32_t>(sizeof(_audio_buffer)), bytes_left);
        const size_t bytes_read = file.read(_audio_buffer, to_read);
        if (bytes_read == 0) {
            Serial.print("Audio read ended early: ");
            Serial.println(audio_file);
            file.close();
            shutdownAudioOutput();
            return PlaybackResult::Failed;
        }

        writeAudioChunk(_audio_buffer, bytes_read, wav_info);
        bytes_left -= bytes_read;
    }

    if (!_task_running && bytes_left > 0) {
        file.close();
        return PlaybackResult::Interrupted;
    }

    file.close();
    if (append_end_silence) {
        writeSilence(wav_info.sample_rate, SPEAKER_END_SILENCE_MS);
    }
    _last_output_ms = millis();
    return PlaybackResult::Finished;
}

void SpeakerController::writeAudioChunk(uint8_t* data, size_t bytes_read, const WavInfo& info)
{
    if (info.channels == 2) {
        int16_t* samples = reinterpret_cast<int16_t*>(data);
        const size_t sample_count = bytes_read / sizeof(int16_t);

        for (size_t i = 0; i < sample_count; i++) {
            samples[i] = applyVolume(samples[i]);
        }

        _i2s.write(data, bytes_read);
        return;
    }

    const size_t sample_count = bytes_read / sizeof(int16_t);
    const int16_t* mono_samples = reinterpret_cast<const int16_t*>(data);

    for (size_t i = 0; i < sample_count; i++) {
        const int16_t sample = applyVolume(mono_samples[i]);
        _stereo_buffer[i * 2] = sample;
        _stereo_buffer[i * 2 + 1] = sample;
    }

    _i2s.write(
        reinterpret_cast<const uint8_t*>(_stereo_buffer),
        sample_count * 2 * sizeof(int16_t));
}

int16_t SpeakerController::applyVolume(int16_t sample) const
{
    const int32_t scaled = (static_cast<int32_t>(sample) * volumeValue(_current_volume)) / 100;

    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(scaled);
}
