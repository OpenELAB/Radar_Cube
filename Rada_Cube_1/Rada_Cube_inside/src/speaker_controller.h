#ifndef SPEAKER_CONTROLLER_H
#define SPEAKER_CONTROLLER_H

#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pins.h"

// ============================================================
// 扬声器中间层接口
// ============================================================
//
// 设计边界：
// - I2SClass + LittleFS + WAV 解析负责真正播放音频。
// - SpeakerController 只给上层提供通用 WAV 播放 API。
// - 上层只调用 playOnce(path) / playLoop(path) / stop 等接口。
// - Speaker task 是唯一真正读 WAV、写 I2S 的地方。
// - 具体业务音效 ID 和文件名映射不属于本驱动层。
//
// 当前蜂鸣节奏方案：
// - 不再由代码用定时器拼出不同蜂鸣周期。
// - 不同速度/节奏的蜂鸣声做成不同 WAV 文件。
// - playLoop() 负责循环播放指定 WAV，周期由 WAV 文件内容本身决定。
// - 命令队列长度为 1，使用覆盖式投递，只保留最新声音命令。

// ======================== 硬件与任务配置 ========================

// I2S 引脚。
// #define SPEAKER_I2S_LRC_PIN                 19
// #define SPEAKER_I2S_BCLK_PIN                20
// #define SPEAKER_I2S_DIN_PIN                 21
// #define SPEAKER_SHUTDOWN_PIN                7

// 功放 shutdown 控制。
#define SPEAKER_SHUTDOWN_ACTIVE_LEVEL       LOW
#define SPEAKER_ENABLE_SETTLE_MS            5     // 功放使能后等待稳定时间
#define SPEAKER_END_SILENCE_MS              80    // 单次播放结束后补静音，避免尾音爆音
#define SPEAKER_SHUTDOWN_SETTLE_MS          2     // 关闭功放后的稳定等待时间
#define SPEAKER_IDLE_SHUTDOWN_MS            500   // 静音空闲多久后关闭 I2S/功放

// 软件音量，范围 0-100。
#define SPEAKER_DEFAULT_VOLUME              50
#define SPEAKER_MAX_VOLUME                  100

#define SPEAKER_VOLUME_LOW_VALUE            30
#define SPEAKER_VOLUME_MED_VALUE            50
#define SPEAKER_VOLUME_HIGH_VALUE           90

// 播放任务和音频缓冲配置。
#define SPEAKER_AUDIO_BUFFER_BYTES          1024
#define SPEAKER_TASK_STACK_WORDS            8192
#define SPEAKER_TASK_PRIORITY               1     // 默认不高于 Arduino loop / ESP-IDF main task
#define SPEAKER_COMMAND_QUEUE_LENGTH        1
#define SPEAKER_TASK_IDLE_DELAY_MS          10
#define SPEAKER_INTERRUPT_SILENCE_MS        20
#define SPEAKER_FADE_MS                     30

#define SPEAKER_PATH_MAX_LENGTH             64

// ======================== 数据结构 ========================

enum class SpeakerMode : uint8_t {
    Silent,      // 静音
    PlayOnce,    // 播放一次 WAV
    Loop         // 循环播放 WAV
};

enum class SpeakerVolumeLevel : uint8_t {
    Default,     // 使用默认音量
    Low,         // 低音量
    Medium,      // 中音量
    High         // 高音量
};

struct SpeakerCommand {
    SpeakerMode mode = SpeakerMode::Silent;
    char path[SPEAKER_PATH_MAX_LENGTH] = {0};

    // 当前保留为命令字段，实际默认使用 setVolume() 设置的全局音量。
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

// ======================== 控制类 ========================

class SpeakerController {
public:
    SpeakerController();
    ~SpeakerController();

    // 初始化 LittleFS、命令队列和 speaker task。
    bool begin();

    // 停止 speaker task、关闭 I2S/功放并释放队列。
    void end();

    // 设置默认音量档位。
    bool setVolume(SpeakerVolumeLevel volume);
    SpeakerVolumeLevel volume() const;

    // 主程序使用的声音 API。
    // 这些接口只负责投递命令，不直接读文件或写 I2S。
    bool stop();
    bool playOnce(const char* wav_path);
    bool playLoop(const char* wav_path);
    void setKeepOutputAlive(bool enabled);
    bool keepOutputAlive() const;

    bool isBegun() const;
    bool isTaskRunning() const;
    SpeakerMode currentMode() const;
    const char* currentPath() const;
    bool lastPlaybackFailed() const;

private:
    I2SClass _i2s;

    QueueHandle_t _command_queue = nullptr;
    TaskHandle_t _task_handle = nullptr;

    SpeakerCommand _current_command;
    SpeakerMode _current_mode = SpeakerMode::Silent;
    char _current_path[SPEAKER_PATH_MAX_LENGTH] = {0};
    SpeakerVolumeLevel _current_volume = SpeakerVolumeLevel::Default;
    bool _begun = false;
    bool _task_running = false;
    bool _i2s_started = false;
    bool _speaker_enabled = false;
    bool _keep_output_alive = false;
    bool _last_playback_failed = false;
    bool _fade_in_next_playback = false;
    int16_t _last_left_sample = 0;
    int16_t _last_right_sample = 0;
    uint32_t _fade_in_frames_left = 0;
    uint32_t _fade_in_frames_total = 0;
    uint32_t _i2s_sample_rate = 0;
    uint32_t _last_output_ms = 0;

    uint8_t _audio_buffer[SPEAKER_AUDIO_BUFFER_BYTES];
    int16_t _stereo_buffer[SPEAKER_AUDIO_BUFFER_BYTES];

    // 私有 task 框架。begin() 创建 task，end() 停止并删除 task。
    static void taskEntry(void* arg);
    void taskLoop();

    // 统一投递命令，内部使用 xQueueOverwrite() 覆盖旧命令。
    bool sendCommand(const SpeakerCommand& command);

    uint8_t volumeValue(SpeakerVolumeLevel volume) const;
    bool readWavInfo(File& file, WavInfo& info);
    bool startI2S(const WavInfo& info);
    void stopI2S();
    void setSpeakerEnabled(bool enabled);
    void writeSilence(uint32_t sample_rate, uint16_t duration_ms);
    void writeFadeToSilence(uint32_t sample_rate, uint16_t duration_ms);
    int16_t applyFadeIn(int16_t sample);
    void rememberOutputSample(int16_t left, int16_t right);
    void resetOutputSamples();
    void shutdownAudioOutput();
    enum class PlaybackResult : uint8_t {
        Finished,
        Interrupted,
        Failed
    };

    PlaybackResult playWav(const char* wav_path, bool append_end_silence);
    void writeAudioChunk(uint8_t* data, size_t bytes_read, const WavInfo& info);
    int16_t applyVolume(int16_t sample) const;
};

// ============================================================
// 主程序调用示例
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
//     Speaker.playOnce(path_from_upper_layer);
// }
//
// void onPairSuccess()
// {
//     Speaker.playOnce(path_from_upper_layer);
// }
//
// void onConnectionLost()
// {
//     Speaker.playOnce(path_from_upper_layer);
// }
//
// void updateParkingDistance(uint16_t distance_cm)
// {
//     if (distance_cm > 220) {
//         Speaker.stop();
//     } else if (distance_cm > 170) {
//         Speaker.playLoop(path_from_upper_layer);
//     } else if (distance_cm > 120) {
//         Speaker.playLoop(path_from_upper_layer);
//     } else if (distance_cm > 70) {
//         Speaker.playLoop(path_from_upper_layer);
//     } else if (distance_cm > 30) {
//         Speaker.playLoop(path_from_upper_layer);
//     } else {
//         Speaker.playLoop(path_from_upper_layer);
//     }
// }
//
// void beforeDeepSleep()
// {
//     Speaker.stop();
//     Speaker.end();
// }

#endif
