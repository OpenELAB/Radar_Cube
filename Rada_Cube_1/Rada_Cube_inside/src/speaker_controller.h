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

// 软件音量范围为 0..100，默认值在每次 begin() 时恢复。
#define SPEAKER_DEFAULT_VOLUME 50
#define SPEAKER_MAX_VOLUME 100

// task 已实际应用的物理播放模式，不表示尚未消费的排队命令。
enum SpeakerMode : uint8_t {
    SPEAKER_MODE_SILENT = 0,
    SPEAKER_MODE_PLAY_ONCE,
    SPEAKER_MODE_LOOP
};

/*
 * Speaker 音频硬件控制器。
 *
 * SpeakerController 只执行调用方指定路径的物理音频播放，不理解业务事件、
 * AudioId、路径映射或业务优先级。唯一的 speaker task 独占 LittleFS 文件读取、
 * I2S 写入、功放控制和实际播放状态。
 *
 * 播放命令队列长度为 1，新命令覆盖尚未执行的旧命令，不实现提示音排队。
 * playOnce() 可以临时打断 playLoop()，一次播放正常结束且期间没有更新命令时，
 * 自动恢复仍然有效的最新 loop。返回 bool 的播放 API 只表示命令是否被接受，
 * 不表示声音已经开始或播放完成。
 */
class SpeakerController {
public:
    SpeakerController();
    ~SpeakerController();

    /*
     * 初始化 LittleFS、同步对象、长度为 1 的命令队列和 speaker task。
     * 成功后处于静音模式并使用默认音量；重复调用保持幂等。
     */
    bool begin();

    /*
     * 停止接收新命令，协作退出 speaker task，关闭 I2S 和功放并释放资源。
     * task 超时未退出时才执行强制清理；重复调用不产生额外效果。
     */
    void end();

    /*
     * 设置目标软件音量，输入钳制到 SPEAKER_MAX_VOLUME。
     * 音量不占用播放队列，当前播放会从下一个音频 chunk 开始使用新值。
     */
    bool setVolume(uint8_t volume);

    // 返回最近一次成功设置且已经钳制的目标音量。
    uint8_t volume() const;

    /*
     * 非阻塞投递一次性播放。audio_path 是借用指针，必须在播放及可能的 loop
     * 恢复期间保持有效，SpeakerController 不复制也不释放该字符串。
     *
     * 新的 playOnce() 会替换尚未完成的旧 once；如果存在有效的最新 loop，
     * 最后一个 once 正常结束后恢复该 loop。命令被接受不代表播放已经完成。
     */
    bool playOnce(const char* audio_path);

    /*
     * 非阻塞替换并尽快开始最新 loop。audio_path 使用与 playOnce() 相同的借用语义。
     * 新 loop 会取消当前 once 的 busy 和旧恢复意图；loop 本身不计入 busy。
     */
    bool playLoop(const char* audio_path);

    /*
     * 非阻塞停止当前声音，并在命令被接受时同步清除最新 loop、恢复意图和 busy。
     * stop() 不清除 lastPlaybackFailed() 保存的历史失败状态。
     */
    bool stop();

    // 一次性播放已入队或正在执行时返回 true；持续 loop 不算 busy。
    bool isBusy() const;

    // begin() 成功且尚未进入 end() 时返回 true。
    bool isBegun() const;

    // speaker task 已创建且尚未退出时返回 true。
    bool isTaskRunning() const;

    /*
     * 返回 speaker task 已实际应用的模式和路径，不返回尚未消费的排队命令。
     * 静音或 end() 完成后 currentAudioPath() 返回 nullptr。
     */
    SpeakerMode currentMode() const;
    const char* currentAudioPath() const;

    /*
     * 最新播放命令发生文件、WAV 或 I2S 错误时返回 true。
     * 该状态在 begin() 或新的播放命令被接受时清除；stop() 和音量更新不清除。
     */
    bool lastPlaybackFailed() const;

private:
    static constexpr size_t AUDIO_BUFFER_BYTES = 1024;

    enum CommandType : uint8_t {
        COMMAND_STOP = 0,
        COMMAND_PLAY_ONCE,
        COMMAND_PLAY_LOOP
    };

    // 路径是借用指针；sequence 用于阻止过期命令修改最新状态。
    struct Command {
        CommandType type = COMMAND_STOP;
        const char* audio_path = nullptr;
        uint32_t sequence = 0;
    };

    // 第一版只接受 16-bit PCM 单声道或双声道 WAV。
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

    // 队列只保存 stop/playOnce/playLoop 中的最新命令，音量不占用该队列。
    QueueHandle_t _command_queue = nullptr;

    // 锁顺序固定为 api -> queue -> state；speaker task 永不获取 api 锁。
    SemaphoreHandle_t _api_mutex = nullptr;
    SemaphoreHandle_t _queue_mutex = nullptr;
    SemaphoreHandle_t _task_stopped = nullptr;
    TaskHandle_t _task_handle = nullptr;
    mutable portMUX_TYPE _state_mux = portMUX_INITIALIZER_UNLOCKED;

    // 以下跨 task 状态统一由 _state_mux 保护。
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

    // 以下播放和硬件状态由 speaker task 独占；end() 强制清理兜底除外。
    bool _i2s_started = false;
    bool _speaker_enabled = false;
    bool _fade_in_next_playback = false;
    int16_t _last_left_sample = 0;
    int16_t _last_right_sample = 0;
    uint32_t _fade_in_frames_left = 0;
    uint32_t _fade_in_frames_total = 0;
    uint32_t _i2s_sample_rate = 0;
    uint32_t _last_output_ms = 0;

    // WAV 数据会按 int16_t 访问，显式保证缓冲区满足采样类型的对齐要求。
    alignas(int16_t) uint8_t _audio_buffer[AUDIO_BUFFER_BYTES];
    int16_t _stereo_buffer[AUDIO_BUFFER_BYTES];

    static void taskEntry(void* arg);

    // 消费最新命令并执行当前模式；循环播放按文件结束点重新开始。
    void taskLoop();

    // 非阻塞提交播放命令，并同步维护 sequence、loop 恢复意图和 busy。
    bool sendPlaybackCommand(CommandType type, const char* audio_path);

    // speaker task 非阻塞读取队列；未取得队列锁时留到下一轮处理。
    bool receiveCommand(Command& command);

    // 仅应用仍为最新 sequence 的命令，过期命令直接丢弃。
    bool handleCommand(const Command& command);

    // once 仅在 sequence 未变化时结束或恢复最新 loop。
    void finishOnce(uint32_t finished_sequence);

    // 播放失败仅能修改仍为最新且正在执行的命令状态。
    void failPlayback(uint32_t failed_sequence);

    bool taskShouldRun() const;
    void setTaskRunning(bool running);
    void setTaskHandle(TaskHandle_t task_handle);

    // 解析并校验 RIFF/WAVE、fmt 和 data 块边界及 PCM 格式。
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
     * 播放一个 WAV 文件，并在每个音频 chunk 前检查最新命令。
     * append_end_silence 仅用于 once 正常结束时的淡出和尾部静音。
     */
    PlaybackResult playWav(const char* audio_path, bool append_end_silence);
    bool writeAudioChunk(uint8_t* data,
                         size_t bytes_read,
                         const WavInfo& info,
                         uint8_t volume);
    int16_t applyVolume(int16_t sample, uint8_t volume) const;
};

#endif
