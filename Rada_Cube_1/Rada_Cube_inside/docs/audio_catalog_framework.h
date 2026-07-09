#ifndef AUDIO_CATALOG_FRAMEWORK_H
#define AUDIO_CATALOG_FRAMEWORK_H

/*
 * AudioCatalog 第一版框架草稿
 *
 * 目标：
 * - 集中管理音频资源 ID 和 LittleFS 路径。
 * - FeedbackController 使用 AudioId 选择音频。
 * - SpeakerController 不包含本文件，只接收 const char* path 并播放。
 * - 倒车距离显示期间是否允许播放，由 FeedbackController 判断。
 *
 * 这个文件只提供：
 * - AudioId 枚举。
 * - audio_path_from_id(audio_id)：AudioId -> 路径。
 * - audio_is_blocked_during_parking(audio_id)：判断某个音频是否属于倒车期间黑名单。
 */

#include <Arduino.h>

#define AUDIO_PATH_BEEP_SLOW             "/beep_slow.wav"
#define AUDIO_PATH_BEEP_MEDIUM_SLOW      "/beep_medium_slow.wav"
#define AUDIO_PATH_BEEP_MEDIUM           "/beep_medium.wav"
#define AUDIO_PATH_BEEP_FAST             "/beep_fast.wav"
#define AUDIO_PATH_BEEP_CONTINUOUS       "/beep_continuous.wav"

#define AUDIO_PATH_BOOT                  "/boot.wav"
#define AUDIO_PATH_SHUTDOWN              "/shutdown.wav"
#define AUDIO_PATH_UNPAIRED              "/mode_unpaired.wav"
#define AUDIO_PATH_PAIRING               "/mode_pairing.wav"
#define AUDIO_PATH_WAKE_OK               "/wake_ok.wav"

#define AUDIO_PATH_TONE_SUCCESS_UP       "/tone_success_up.wav"
#define AUDIO_PATH_TONE_WARNING_DOWN     "/tone_warning_down.wav"

#define AUDIO_PATH_PAIR_OK_BOTH          "/pair_ok_both.wav"
#define AUDIO_PATH_PAIR_FAIL_LEFT        "/pair_fail_left.wav"
#define AUDIO_PATH_PAIR_FAIL_RIGHT       "/pair_fail_right.wav"
#define AUDIO_PATH_PAIR_FAIL_BOTH        "/pair_fail_both.wav"

#define AUDIO_PATH_CONNECTION_LOST       "/connection_lost.wav"
#define AUDIO_PATH_LOW_BATTERY           "/low_battery.wav"
#define AUDIO_PATH_FAULT                 "/fault.wav"

/*
 * AudioId 只属于 AudioCatalog 和 FeedbackController。
 * 不要把 AudioId 放进 SpeakerController。
 */
enum AudioId : uint8_t {
    AUDIO_ID_NONE = 0,

    AUDIO_ID_BEEP_SLOW,
    AUDIO_ID_BEEP_MEDIUM_SLOW,
    AUDIO_ID_BEEP_MEDIUM,
    AUDIO_ID_BEEP_FAST,
    AUDIO_ID_BEEP_CONTINUOUS,

    AUDIO_ID_BOOT,
    AUDIO_ID_SHUTDOWN,
    AUDIO_ID_UNPAIRED,
    AUDIO_ID_PAIRING,
    AUDIO_ID_WAKE_OK,

    AUDIO_ID_TONE_SUCCESS_UP,
    AUDIO_ID_TONE_WARNING_DOWN,

    AUDIO_ID_PAIR_OK_BOTH,
    AUDIO_ID_PAIR_FAIL_LEFT,
    AUDIO_ID_PAIR_FAIL_RIGHT,
    AUDIO_ID_PAIR_FAIL_BOTH,

    AUDIO_ID_CONNECTION_LOST,
    AUDIO_ID_LOW_BATTERY,
    AUDIO_ID_FAULT
};

/*
 * 根据 AudioId 返回 LittleFS 路径。
 */
static const char *audio_path_from_id(AudioId audio_id)
{
    switch (audio_id) {
    case AUDIO_ID_BEEP_SLOW:
        return AUDIO_PATH_BEEP_SLOW;
    case AUDIO_ID_BEEP_MEDIUM_SLOW:
        return AUDIO_PATH_BEEP_MEDIUM_SLOW;
    case AUDIO_ID_BEEP_MEDIUM:
        return AUDIO_PATH_BEEP_MEDIUM;
    case AUDIO_ID_BEEP_FAST:
        return AUDIO_PATH_BEEP_FAST;
    case AUDIO_ID_BEEP_CONTINUOUS:
        return AUDIO_PATH_BEEP_CONTINUOUS;

    case AUDIO_ID_BOOT:
        return AUDIO_PATH_BOOT;
    case AUDIO_ID_SHUTDOWN:
        return AUDIO_PATH_SHUTDOWN;
    case AUDIO_ID_UNPAIRED:
        return AUDIO_PATH_UNPAIRED;
    case AUDIO_ID_PAIRING:
        return AUDIO_PATH_PAIRING;
    case AUDIO_ID_WAKE_OK:
        return AUDIO_PATH_WAKE_OK;

    case AUDIO_ID_TONE_SUCCESS_UP:
        return AUDIO_PATH_TONE_SUCCESS_UP;
    case AUDIO_ID_TONE_WARNING_DOWN:
        return AUDIO_PATH_TONE_WARNING_DOWN;

    case AUDIO_ID_PAIR_OK_BOTH:
        return AUDIO_PATH_PAIR_OK_BOTH;
    case AUDIO_ID_PAIR_FAIL_LEFT:
        return AUDIO_PATH_PAIR_FAIL_LEFT;
    case AUDIO_ID_PAIR_FAIL_RIGHT:
        return AUDIO_PATH_PAIR_FAIL_RIGHT;
    case AUDIO_ID_PAIR_FAIL_BOTH:
        return AUDIO_PATH_PAIR_FAIL_BOTH;

    case AUDIO_ID_CONNECTION_LOST:
        return AUDIO_PATH_CONNECTION_LOST;
    case AUDIO_ID_LOW_BATTERY:
        return AUDIO_PATH_LOW_BATTERY;
    case AUDIO_ID_FAULT:
        return AUDIO_PATH_FAULT;

    case AUDIO_ID_NONE:
    default:
        return nullptr;
    }
}

/*
 * 倒车距离显示期间的音频黑名单。
 *
 * FeedbackController 在进入倒车距离显示状态后，调用这个函数决定是否拦截。
 * SpeakerController 不知道倒车状态，也不做这个判断。
 *
 * 第一版规则：
 * - 距离蜂鸣 loop 不在黑名单里。
 * - 短提示音不在黑名单里，可以短暂打断距离蜂鸣。
 * - 语音类、模式类、最终结果类提示在黑名单里。
 */
static bool audio_is_blocked_during_parking(AudioId audio_id)
{
    switch (audio_id) {
    case AUDIO_ID_BOOT:
    case AUDIO_ID_SHUTDOWN:
    case AUDIO_ID_UNPAIRED:
    case AUDIO_ID_PAIRING:
    case AUDIO_ID_WAKE_OK:
    case AUDIO_ID_PAIR_OK_BOTH:
    case AUDIO_ID_PAIR_FAIL_LEFT:
    case AUDIO_ID_PAIR_FAIL_RIGHT:
    case AUDIO_ID_PAIR_FAIL_BOTH:
    case AUDIO_ID_CONNECTION_LOST:
    case AUDIO_ID_LOW_BATTERY:
    case AUDIO_ID_FAULT:
        return true;

    case AUDIO_ID_BEEP_SLOW:
    case AUDIO_ID_BEEP_MEDIUM_SLOW:
    case AUDIO_ID_BEEP_MEDIUM:
    case AUDIO_ID_BEEP_FAST:
    case AUDIO_ID_BEEP_CONTINUOUS:
    case AUDIO_ID_TONE_SUCCESS_UP:
    case AUDIO_ID_TONE_WARNING_DOWN:
    case AUDIO_ID_NONE:
    default:
        return false;
    }
}

#endif
