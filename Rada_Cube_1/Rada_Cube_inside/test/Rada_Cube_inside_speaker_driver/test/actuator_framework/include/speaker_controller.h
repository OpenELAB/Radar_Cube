#ifndef SPEAKER_CONTROLLER_H
#define SPEAKER_CONTROLLER_H

#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ============================================================
// 扬声器中间层接口设计稿
// ============================================================
//
// 设计边界：
// - I2SClass + LittleFS + WAV 解析负责真正播放音频。
// - SpeakerController 只给主程序提供简单声音 API。
// - 主程序只调用 playOnce / beep / stop 等接口。
// - Speaker task 是唯一真正读 WAV、写 I2S 的地方。
//
// 这里假设“滴滴声”也是 WAV 文件，不再直接用 PWM/tone 产生声音。
// 倒车雷达工作时，声音表达的是当前危险等级，所以队列只保留最新命令。

// ======================== 配置宏 ========================

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
#define SPEAKER_TASK_PRIORITY               1     // 默认不高于 Arduino loop / ESP-IDF main task；主控制独立成高优先级 task 后可改为 2
#define SPEAKER_COMMAND_QUEUE_LENGTH        1
#define SPEAKER_TASK_IDLE_DELAY_MS          10

// 5 档周期滴声。后续实际听感需要调整时，只改这些宏。
#define SPEAKER_BEEP_PERIOD_VERY_SLOW_MS    1500
#define SPEAKER_BEEP_PERIOD_SLOW_MS         1000
#define SPEAKER_BEEP_PERIOD_MEDIUM_MS       600
#define SPEAKER_BEEP_PERIOD_FAST_MS         300
#define SPEAKER_BEEP_PERIOD_DANGER_MS       180

// WAV 文件名先占位，最终音频确定后替换 data/ 下的文件即可。
#define SPEAKER_FILE_BEEP_SHORT             "/beep_short.wav"
#define SPEAKER_FILE_BEEP_DANGER            "/beep_danger.wav"
#define SPEAKER_FILE_BOOT                   "/boot.wav"
#define SPEAKER_FILE_PAIR_OK                "/pair_ok.wav"
#define SPEAKER_FILE_PAIR_FAIL              "/pair_fail.wav"
#define SPEAKER_FILE_WAKE_FAIL              "/wake_fail.wav"
#define SPEAKER_FILE_LOW_BATTERY            "/low_battery.wav"
#define SPEAKER_FILE_FAULT                  "/fault.wav"

// ======================== 数据结构 ========================

enum class AudioId : uint8_t {
    None,
    BeepShort,      // 普通滴声
    BeepDanger,     // 危险距离滴声
    Boot,           // 上电/启动提示
    PairOk,         // 配对成功
    PairFail,       // 配对失败
    WakeFail,       // 唤醒从机失败
    LowBattery,     // 低电量
    Fault           // 硬件/通信异常
};

enum class SpeakerMode : uint8_t {
    Silent,      // 静音
    PlayOnce,    // 播放一次 WAV
    Periodic     // 按固定档位周期播放 WAV
};

enum class BeepLevel : uint8_t {
    VerySlow,    // 很慢，例如远距离
    Slow,        // 慢速
    Medium,      // 中速
    Fast,        // 快速
    Danger       // 危险，接近连续急促提示
};

enum class SpeakerVolumeLevel : uint8_t {
    Default,     // 使用当前默认音量
    Low,         // 低音量
    Medium,      // 中音量
    High         // 高音量
};

struct SpeakerCommand {
    SpeakerMode mode = SpeakerMode::Silent;
    AudioId audio = AudioId::None;

    // Periodic 模式使用，不直接暴露 ms 给主程序。
    BeepLevel level = BeepLevel::Slow;

    // 使用音量档位，具体数值由 SPEAKER_VOLUME_*_VALUE 宏决定。
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

    // 初始化 LittleFS、I2S 相关状态、创建命令队列、创建 speaker task。
    bool begin();

    // 停止 speaker task、停止 I2S、删除命令队列。
    void end();

    // 设置默认音量档位，具体数值由宏决定。
    bool setVolume(SpeakerVolumeLevel volume);
    SpeakerVolumeLevel volume() const;

    // 主程序使用的声音 API。
    // 这些接口只负责投递命令，不直接播放 WAV。
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

    // 私有 task 框架。
    // begin() 内部创建 task，end() 内部删除 task。
    static void taskEntry(void* arg);
    void taskLoop();

    // 统一投递命令，内部使用 xQueueOverwrite() 覆盖旧命令。
    bool sendCommand(const SpeakerCommand& command);
};

// ============================================================
// Speaker task 实现建议
// ============================================================
//
// taskLoop() 推荐结构：
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
//         // 保持静音，必要时停止当前 I2S 输出。
//     }
//
//     if (_current_mode == SpeakerMode::PlayOnce) {
//         // 播放一次 _current_audio 对应的 WAV。
//         // 播放完成后切回 Silent。
//     }
//
//     if (_current_mode == SpeakerMode::Periodic) {
//         // 根据 BeepLevel 查表得到周期。
//         // 到时间就播放一次 _current_audio 对应的 WAV。
//     }
//
//     vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
// }

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
