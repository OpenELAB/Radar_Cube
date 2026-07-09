#ifndef SPEAKER_CONTROLLER_FRAMEWORK_H
#define SPEAKER_CONTROLLER_FRAMEWORK_H

/*
 * SpeakerController 第一版框架草稿
 *
 * 目标：
 * - 只做音频播放驱动，不理解配对、唤醒、距离、失联等业务事件。
 * - 不包含 AudioCatalog，不管理 AudioId 到路径的映射。
 * - 保留一个 speaker task 独占 I2S、功放和播放状态。
 * - 对 FeedbackController 只暴露 playOnce()、playLoop()、stop()、音量和状态查询。
 * - 不实现多元素提示队列，不实现业务优先级，不实现语音排队。
 *
 * 关键播放语义：
 * - playLoop(path)：设置并立即播放最新 loop，主要用于距离蜂鸣。
 * - playOnce(path)：如果当前是 playLoop(loop_path)，临时打断 loop_path，播放 path。
 * - playOnce(path)：如果当前是 playOnce(old_path)，直接切歌到 path；old_path 不恢复。
 * - playOnce(path) 正常结束后，如果期间没有新的播放命令，自动恢复被打断的最新 loop。
 * - stop()：停止当前声音，并清空待恢复 loop。
 */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pins.h"

#define SPEAKER_DEFAULT_VOLUME              50
#define SPEAKER_MAX_VOLUME                  100
#define SPEAKER_TASK_STACK_WORDS            8192
#define SPEAKER_TASK_PRIORITY               1
#define SPEAKER_COMMAND_QUEUE_LENGTH        1

enum SpeakerMode : uint8_t {
    SPEAKER_MODE_SILENT = 0,
    SPEAKER_MODE_PLAY_ONCE,
    SPEAKER_MODE_LOOP
};

class SpeakerController {
public:
    SpeakerController();
    ~SpeakerController();

    /*
     * 初始化音频硬件、命令队列和 speaker task。
     * 具体 LittleFS、WAV、I2S、功放初始化放在 .cpp 里完成。
     */
    bool begin();

    /*
     * 停止播放，释放 task 和队列，关闭音频硬件。
     */
    void end();

    /*
     * 设置默认音量，范围 0..SPEAKER_MAX_VOLUME。
     * 实现里应做 clamp。
     */
    bool setVolume(uint8_t volume);
    uint8_t volume() const;

    /*
     * 播放一次音频。
     *
     * audio_path 必须是稳定有效的静态字符串，不能传临时 buffer。
     * FeedbackController 通过 AudioCatalog 查到 path 后再传进来。
     */
    bool playOnce(const char* audio_path);

    /*
     * 设置并立即播放最新 loop。
     * 距离蜂鸣使用 playLoop。新的 playLoop 会替换旧 loop。
     */
    bool playLoop(const char* audio_path);

    /*
     * 停止当前音频，并清空待恢复 loop。
     */
    bool stop();

    /*
     * 一次性音频正在播放，或一次性音频打断 loop 后尚未恢复 loop 时返回 true。
     * 持续 loop 本身不算需要业务等待的 busy。
     */
    bool isBusy() const;

    bool isBegun() const;
    bool isTaskRunning() const;
    SpeakerMode currentMode() const;
    const char* currentAudioPath() const;
    bool lastPlaybackFailed() const;

private:
    enum CommandType : uint8_t {
        COMMAND_STOP = 0,
        COMMAND_PLAY_ONCE,
        COMMAND_PLAY_LOOP,
        COMMAND_SET_VOLUME
    };

    struct Command {
        CommandType type = COMMAND_STOP;
        const char* audio_path = nullptr;
        uint8_t volume = SPEAKER_DEFAULT_VOLUME;
        uint32_t sequence = 0;
    };

    /*
     * 具体 QueueHandle_t、TaskHandle_t、I2S 对象和状态变量，
     * 第一版框架不在头文件里展开，由实现同事根据当前代码补充。
     *
     * 但实现里建议至少维护：
     * - 当前播放模式。
     * - 当前 audio_path。
     * - 最新 loop audio_path。
     * - 最新命令序号 sequence。
     * - 是否需要在 playOnce 结束后恢复 loop。
     */

    static void taskEntry(void* arg);

    /*
     * taskLoop() 建议主流程：
     * 1. 从队列读取最新 Command；有新命令就调用 handleCommand()。
     * 2. 播放过程中也要周期性检查队列，保证 playOnce/playLoop/stop 能打断当前播放。
     * 3. playOnce 正常结束后，调用 restoreLoopIfNeeded(finished_sequence)。
     */
    void taskLoop();

    /*
     * 所有 public API 最终都构造 Command 并调用 sendCommand()。
     * sendCommand() 使用 xQueueOverwrite()，保证 task 只执行最新命令。
     * public API 不直接操作 I2S 或功放。
     */
    bool sendCommand(const Command& command);

    /*
     * speaker task 的命令入口。
     * 这里决定 stop、playOnce、playLoop、setVolume 如何影响当前播放状态。
     */
    void handleCommand(const Command& command);

    /*
     * task 内部播放入口。
     * 具体如何打开文件、解析 WAV、写 I2S、控制功放，由实现同事完成。
     */
    bool playAudioOnce(const char* audio_path, uint32_t command_sequence);
    bool playAudioLoop(const char* audio_path);
    void stopAudio();

    /*
     * playOnce 正常结束后尝试恢复 loop。
     *
     * 只有当 finished_sequence 仍然是最新命令序号时才恢复。
     * 如果 playOnce 期间已经来了新的 playOnce/playLoop/stop，就不恢复旧 loop，
     * 避免恢复已经过期的距离蜂鸣。
     */
    void restoreLoopIfNeeded(uint32_t finished_sequence);
};

/*
 * 最小实现策略
 *
 * - begin()/end()/taskEntry()/taskLoop() 可以复用当前 speaker controller 实现。
 * - playLoop(path) 记录最新 loop path，并立即播放。
 * - playOnce(path) 如果打断 loop，记录需要恢复；如果打断 playOnce，不恢复旧的一次音频。
 * - playOnce 正常结束后，只有没有新命令覆盖时才恢复最新 loop。
 * - stop() 停止当前播放，并清空 latest loop 和 restore 标志。
 * - 不增加多元素提示队列。需要顺序播放时，由 main 根据 FeedbackController::isBusy() 控制调用时机。
 *
 *
 * 接口适配建议
 *
 * 当前 src/speaker_controller 里的 WAV 解析、I2S 初始化、功放 shutdown、
 * fade in/out、播放过程中检查队列这些底层实现可以保留。
 *
 * 需要迁移/删除的部分：
 * - AudioId enum 和 audioFilePath() 从 SpeakerController 移出，放到 AudioCatalog。
 * - playOnce(AudioId) 改成 playOnce(const char* audio_path)。
 * - playLoop(AudioId) 改成 playLoop(const char* audio_path)。
 * - currentAudio() 改成 currentAudioPath()。
 * - SpeakerController 不判断某个音频是不是配对、失联、低电或距离蜂鸣。
 *
 * audio_path 必须来自 AudioCatalog 中的静态字符串。
 * 不要传局部 char buffer，也不要在 SpeakerController 内保存需要释放的字符串。
 *
 *
 * task 状态建议
 *
 * 第一版实现里建议至少维护这些私有状态：
 *
 * - SpeakerMode _current_mode;
 * - const char* _current_path;
 * - const char* _latest_loop_path;
 * - bool _restore_loop_after_once;
 * - uint32_t _command_sequence;
 * - uint32_t _playing_sequence;
 * - bool _busy;
 *
 * 每收到一个 stop/playOnce/playLoop 命令，都递增 _command_sequence。
 * playOnce 开始播放时记录本次 sequence，播放结束后只在 sequence 没变化时恢复 loop。
 *
 *
 * 命令处理逻辑建议
 *
 * stop:
 * - 停止当前音频。
 * - _current_mode = SPEAKER_MODE_SILENT。
 * - _latest_loop_path = nullptr。
 * - _restore_loop_after_once = false。
 * - _busy = false。
 *
 * playLoop(path):
 * - _latest_loop_path = path。
 * - _restore_loop_after_once = false。
 * - 立即打断当前音频并播放新的 loop。
 * - loop 是持续距离蜂鸣，不让 isBusy() 一直返回 true。
 *
 * playOnce(path):
 * - 如果当前是 SPEAKER_MODE_LOOP，并且 _latest_loop_path 有效，
 *   设置 _restore_loop_after_once = true。
 * - 如果当前是 SPEAKER_MODE_PLAY_ONCE，不恢复被打断的一次性音频。
 * - 立即打断当前音频，播放新的 once。
 * - _busy = true。
 *
 * playOnce 正常结束:
 * - 如果 _command_sequence == finished_sequence，
 *   且 _restore_loop_after_once == true，
 *   且 _latest_loop_path != nullptr，
 *   则切回 SPEAKER_MODE_LOOP 并播放 _latest_loop_path。
 * - 否则进入 SPEAKER_MODE_SILENT。
 * - 最后 _busy = false。
 *
 *
 * 播放循环注意事项
 *
 * 1. taskLoop() 空闲时检查队列；playWav() 播放过程中也必须按 chunk 检查队列。
 *    这样 playOnce/playLoop/stop 才能及时打断当前声音。
 *
 * 2. 命令队列长度保持 1，并使用 xQueueOverwrite()。
 *    SpeakerController 只关心最新播放命令，不做提示音排队。
 *
 * 3. public API 不打开文件、不写 I2S、不控制功放。
 *    LittleFS.open()、I2S.write()、功放开关都放在 speaker task 内。
 *
 * 4. stop() 必须清空待恢复 loop。
 *    否则用户关机/退出倒车后，后续一次性提示播放结束可能错误恢复旧距离蜂鸣。
 *
 * 5. 播放失败时不要无限重试。
 *    设置 lastPlaybackFailed=true，进入 Silent；由上层决定是否降级或忽略。
 *
 * 6. isBusy() 建议只表示一次性提示是否还在播放。
 *    loop 距离蜂鸣是工作状态，不是 main 需要等待完成的任务。
 *
 * 7. currentMode()/currentAudioPath()/isBusy() 这些跨 task 状态查询，
 *    需要用 critical section、mutex，或确保读写是简单且一致的。
 */

#endif
