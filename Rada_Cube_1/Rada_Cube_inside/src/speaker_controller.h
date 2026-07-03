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
// - SpeakerController 只给主程序提供简单声音 API。
// - 主程序只调用 playOnce / playLoop / stop 等接口。
// - Speaker task 是唯一真正读 WAV、写 I2S 的地方。
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
#define SPEAKER_VOLUME_HIGH_VALUE           100

// 播放任务和音频缓冲配置。
#define SPEAKER_AUDIO_BUFFER_BYTES          1024
#define SPEAKER_TASK_STACK_WORDS            8192
#define SPEAKER_TASK_PRIORITY               1     // 默认不高于 Arduino loop / ESP-IDF main task
#define SPEAKER_COMMAND_QUEUE_LENGTH        1
#define SPEAKER_TASK_IDLE_DELAY_MS          10
#define SPEAKER_INTERRUPT_SILENCE_MS        20
#define SPEAKER_FADE_MS                     30

// ======================== WAV 文件配置 ========================
//
// 蜂鸣周期已经做进 WAV 文件内部：
// - dist_beep_far.wav      远距离蜂鸣
// - dist_beep_mid_far.wav  中远距离蜂鸣
// - dist_beep_mid.wav      中距离蜂鸣
// - dist_beep_near.wav     近距离蜂鸣
// - dist_beep_danger.wav   危险距离蜂鸣
//
// 替换 data/ 下同名 WAV 文件即可调整音色、频率、包络和周期。
#define SPEAKER_FILE_DIST_BEEP_FAR              "/dist_beep_far.wav"
#define SPEAKER_FILE_DIST_BEEP_MID_FAR          "/dist_beep_mid_far.wav"
#define SPEAKER_FILE_DIST_BEEP_MID              "/dist_beep_mid.wav"
#define SPEAKER_FILE_DIST_BEEP_NEAR             "/dist_beep_near.wav"
#define SPEAKER_FILE_DIST_BEEP_DANGER           "/dist_beep_danger.wav"
#define SPEAKER_FILE_SYS_BOOT                   "/sys_boot.wav"
#define SPEAKER_FILE_SYS_SHUTDOWN               "/sys_shutdown.wav"
#define SPEAKER_FILE_MODE_UNPAIRED              "/mode_unpaired.wav"
#define SPEAKER_FILE_MODE_PAIRING               "/mode_pairing.wav"
#define SPEAKER_FILE_MODE_FACTORY_RESET_DONE    "/mode_factory_reset_done.wav"
#define SPEAKER_FILE_PAIR_OK_LEFT               "/pair_ok_left.wav"
#define SPEAKER_FILE_PAIR_OK_RIGHT              "/pair_ok_right.wav"
#define SPEAKER_FILE_PAIR_OK_BOTH               "/pair_ok_both.wav"
#define SPEAKER_FILE_PAIR_FAIL_LEFT             "/pair_fail_left.wav"
#define SPEAKER_FILE_PAIR_FAIL_RIGHT            "/pair_fail_right.wav"
#define SPEAKER_FILE_PAIR_FAIL_BOTH             "/pair_fail_both.wav"
#define SPEAKER_FILE_WAKE_START                 "/wake_start.wav"
#define SPEAKER_FILE_WAKE_OK                    "/wake_ok.wav"
#define SPEAKER_FILE_WAKE_FAIL_LEFT             "/wake_fail_left.wav"
#define SPEAKER_FILE_WAKE_FAIL_RIGHT            "/wake_fail_right.wav"
#define SPEAKER_FILE_WAKE_FAIL_BOTH             "/wake_fail_both.wav"
#define SPEAKER_FILE_LINK_LOST_LEFT             "/link_lost_left.wav"
#define SPEAKER_FILE_LINK_LOST_RIGHT            "/link_lost_right.wav"
#define SPEAKER_FILE_LINK_LOST_BOTH             "/link_lost_both.wav"
#define SPEAKER_FILE_LINK_RESTORED_LEFT         "/link_restored_left.wav"
#define SPEAKER_FILE_LINK_RESTORED_RIGHT        "/link_restored_right.wav"
#define SPEAKER_FILE_POWER_LOW                  "/power_low.wav"
#define SPEAKER_FILE_POWER_CRITICAL             "/power_critical.wav"
#define SPEAKER_FILE_POWER_SENSOR_LOW_LEFT      "/power_sensor_low_left.wav"
#define SPEAKER_FILE_POWER_SENSOR_LOW_RIGHT     "/power_sensor_low_right.wav"
#define SPEAKER_FILE_POWER_SENSOR_LOW_BOTH      "/power_sensor_low_both.wav"
#define SPEAKER_FILE_POWER_SENSOR_CRIT_LEFT     "/power_sensor_critical_left.wav"
#define SPEAKER_FILE_POWER_SENSOR_CRIT_RIGHT    "/power_sensor_critical_right.wav"
#define SPEAKER_FILE_POWER_SENSOR_CRIT_BOTH     "/power_sensor_critical_both.wav"
#define SPEAKER_FILE_FAULT_SENSOR_LEFT          "/fault_sensor_left.wav"
#define SPEAKER_FILE_FAULT_SENSOR_RIGHT         "/fault_sensor_right.wav"
#define SPEAKER_FILE_FAULT_SENSOR_BOTH          "/fault_sensor_both.wav"
#define SPEAKER_FILE_FAULT_COMM                 "/fault_comm.wav"
#define SPEAKER_FILE_FAULT_SYSTEM               "/fault_system.wav"

// ======================== 数据结构 ========================

enum class AudioId : uint8_t {
    None,                     // 无音效，用于空命令或非法映射保护

    DistBeepFar,              // 120cm < min_dist <= 150cm，远距离循环蜂鸣
    DistBeepMidFar,           // 90cm < min_dist <= 120cm，中远距离循环蜂鸣
    DistBeepMid,              // 60cm < min_dist <= 90cm，中距离循环蜂鸣
    DistBeepNear,             // 30cm < min_dist <= 60cm，近距离循环蜂鸣
    DistBeepDanger,           // 0cm < min_dist <= 30cm，危险距离连续/高紧急蜂鸣

    SysBoot,                  // 车内主机启动提示
    SysShutdown,              // 准备进入深度睡眠/关机提示
    ModeUnpaired,             // 未保存完整左右车外节点 MAC
    ModePairing,              // 进入无线配对流程
    ModeFactoryResetDone,     // MAC 和 LoRa 配置清除完成

    PairOkLeft,               // 左侧车外节点配对成功
    PairOkRight,              // 右侧车外节点配对成功
    PairOkBoth,               // 左右车外节点均配对成功
    PairFailLeft,             // 配对结束时左侧车外节点未完成
    PairFailRight,            // 配对结束时右侧车外节点未完成
    PairFailBoth,             // 配对结束时左右车外节点均未完成

    WakeStart,                // WORK_MODE 开始 LoRa 唤醒
    WakeOk,                   // 左右车外节点均返回 WAKE_ACK
    WakeFailLeft,             // 左侧车外节点唤醒失败
    WakeFailRight,            // 右侧车外节点唤醒失败
    WakeFailBoth,             // 左右车外节点均唤醒失败

    LinkLostLeft,             // 工作中左侧节点通信失联
    LinkLostRight,            // 工作中右侧节点通信失联
    LinkLostBoth,             // 工作中左右节点均失联
    LinkRestoredLeft,         // 左侧节点失联后重新收到有效数据
    LinkRestoredRight,        // 右侧节点失联后重新收到有效数据

    PowerLow,                 // 车内主机普通低电，仍可继续工作
    PowerCritical,            // 车内主机严重低电，播完后即将关机
    PowerSensorLowLeft,       // 左侧车外节点普通低电
    PowerSensorLowRight,      // 右侧车外节点普通低电
    PowerSensorLowBoth,       // 左右车外节点普通低电
    PowerSensorCriticalLeft,  // 左侧车外节点严重低电
    PowerSensorCriticalRight, // 右侧车外节点严重低电
    PowerSensorCriticalBoth,  // 左右车外节点严重低电，雷达即将不可用

    FaultSensorLeft,          // 左侧雷达连续无效距离达到故障阈值
    FaultSensorRight,         // 右侧雷达连续无效距离达到故障阈值
    FaultSensorBoth,          // 左右雷达均达到故障阈值
    FaultComm,                // 通信/协议异常
    FaultSystem               // 无法归类的系统级故障
};

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
    AudioId audio = AudioId::None;

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
    bool playOnce(AudioId audio);
    bool playLoop(AudioId audio);
    void setKeepOutputAlive(bool enabled);
    bool keepOutputAlive() const;

    bool isBegun() const;
    bool isTaskRunning() const;
    SpeakerMode currentMode() const;
    AudioId currentAudio() const;
    bool lastPlaybackFailed() const;

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

    const char* audioFilePath(AudioId audio) const;
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

    PlaybackResult playWav(AudioId audio, bool append_end_silence);
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
//     Speaker.playOnce(AudioId::SysBoot);
// }
//
// void onPairSuccess()
// {
//     Speaker.playOnce(AudioId::PairOkBoth);
// }
//
// void onConnectionLost()
// {
//     Speaker.playOnce(AudioId::LinkLostBoth);
// }
//
// void updateParkingDistance(uint16_t distance_cm)
// {
//     if (distance_cm > 220) {
//         Speaker.stop();
//     } else if (distance_cm > 170) {
//         Speaker.playLoop(AudioId::DistBeepFar);
//     } else if (distance_cm > 120) {
//         Speaker.playLoop(AudioId::DistBeepMidFar);
//     } else if (distance_cm > 70) {
//         Speaker.playLoop(AudioId::DistBeepMid);
//     } else if (distance_cm > 30) {
//         Speaker.playLoop(AudioId::DistBeepNear);
//     } else {
//         Speaker.playLoop(AudioId::DistBeepDanger);
//     }
// }
//
// void beforeDeepSleep()
// {
//     Speaker.stop();
//     Speaker.end();
// }

#endif
