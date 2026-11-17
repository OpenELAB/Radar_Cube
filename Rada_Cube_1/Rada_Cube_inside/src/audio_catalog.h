#ifndef AUDIO_CATALOG_H
#define AUDIO_CATALOG_H

/*
 * AudioCatalog first version framework draft
 *
 * Goal:
 * - Centrally manage audio resource IDs and LittleFS paths.
 * - FeedbackController selects audio using AudioId.
 * - SpeakerController does not contain this file, it only receives const char* path and plays it.
 * - Whether playback is allowed during the reversing distance display is determined by FeedbackController.
 *
 * This file only provides:
 * - AudioId enum.
 * - audio_path_from_id(audio_id): AudioId -> path.
 * - audio_is_blocked_during_parking(audio_id): Determine whether an audio belongs to the blacklist during reversing.
 */

#include <Arduino.h>

#define AUDIO_PATH_BEEP_SLOW             "/beep_slow.wav"
#define AUDIO_PATH_BEEP_MEDIUM_SLOW      "/beep_medium_slow.wav"
#define AUDIO_PATH_BEEP_MEDIUM           "/beep_medium.wav"
#define AUDIO_PATH_BEEP_FAST             "/beep_fast.wav"
#define AUDIO_PATH_BEEP_CONTINUOUS       "/beep_continuous.wav"

#define AUDIO_PATH_BOOT                  "/boot.wav"
#define AUDIO_PATH_SHUTDOWN              "/sys_shutdown.wav"
#define AUDIO_PATH_UNPAIRED              "/mode_unpaired.wav"
#define AUDIO_PATH_PAIRING               "/mode_pairing.wav"
#define AUDIO_PATH_WAKE_OK               "/wake_ok.wav"

#define AUDIO_PATH_TONE_SUCCESS_UP       "/tone_success_up.wav"
#define AUDIO_PATH_TONE_WARNING_DOWN     "/tone_warning_down.wav"

#define AUDIO_PATH_PAIR_OK_BOTH          "/pair_ok_both.wav"
#define AUDIO_PATH_PAIR_FAIL_LEFT        "/pair_fail_left.wav"
#define AUDIO_PATH_PAIR_FAIL_RIGHT       "/pair_fail_right.wav"
#define AUDIO_PATH_PAIR_FAIL_BOTH        "/pair_fail_both.wav"

#define AUDIO_PATH_CONNECTION_LOST       "/fault_comm.wav"
#define AUDIO_PATH_LOW_BATTERY           "/power_low.wav"
#define AUDIO_PATH_FAULT                 "/fault_system.wav"

/*
 * AudioId only belongs to AudioCatalog and FeedbackController.
 * Don't put AudioId into SpeakerController.
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
 * Return the LittleFS path based on AudioId.
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
 * Audio blacklist during reversing distance display.
 *
 * After FeedbackController enters the reverse distance display state, it calls this function to decide whether to intercept.
 * SpeakerController does not know the reverse status and does not make this judgment.
 *
 * First edition rules:
 * - The distance beep loop is not in the blacklist.
 * - The short beep is not in the blacklist and can briefly interrupt the distance beep.
 * - Voice, mode, and final result prompts are in the blacklist.
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
