#include "../../src/speaker_controller.h"

#include <cstring>

namespace {

constexpr char AUDIO_BOOT[] = "/boot.wav";
constexpr char AUDIO_BEEP_SLOW[] = "/beep_slow.wav";
constexpr char AUDIO_TONE_SUCCESS[] = "/tone_success_up.wav";
constexpr char AUDIO_MISSING[] = "/speaker_test_missing.wav";
constexpr char AUDIO_CORRUPT[] = "/speaker_test_corrupt.wav";

constexpr uint32_t STATE_TIMEOUT_MS = 1500;
constexpr uint32_t ONCE_TIMEOUT_MS = 3500;
constexpr uint32_t LOOP_OBSERVE_MS = 7000;
constexpr uint32_t STOP_NO_RESTORE_MS = 4200;
constexpr uint32_t IDLE_SHUTDOWN_WAIT_MS = 800;
constexpr uint16_t RAPID_COMMAND_COUNT = 600;

SpeakerController speaker;
uint16_t passed_checks = 0;
uint16_t total_checks = 0;

const char* modeName(SpeakerMode mode)
{
    switch (mode) {
    case SPEAKER_MODE_PLAY_ONCE:
        return "PLAY_ONCE";
    case SPEAKER_MODE_LOOP:
        return "LOOP";
    case SPEAKER_MODE_SILENT:
    default:
        return "SILENT";
    }
}

bool pathEquals(const char* expected)
{
    const char* actual = speaker.currentAudioPath();
    if (expected == nullptr || actual == nullptr) {
        return expected == actual;
    }
    return std::strcmp(expected, actual) == 0;
}

void reportCheck(bool passed, const char* description)
{
    total_checks++;
    if (passed) {
        passed_checks++;
    }

    Serial.print(passed ? "[PASS] " : "[FAIL] ");
    Serial.println(description);
}

void reportObservation(const char* description)
{
    Serial.print("[OBSERVE] ");
    Serial.println(description);
}

bool waitForState(SpeakerMode mode,
                  const char* path,
                  uint32_t timeout_ms)
{
    const uint32_t started_at = millis();
    while (millis() - started_at < timeout_ms) {
        if (speaker.currentMode() == mode && pathEquals(path)) {
            return true;
        }
        delay(10);
    }
    return speaker.currentMode() == mode && pathEquals(path);
}

bool waitForBusy(bool expected, uint32_t timeout_ms)
{
    const uint32_t started_at = millis();
    while (millis() - started_at < timeout_ms) {
        if (speaker.isBusy() == expected) {
            return true;
        }
        delay(10);
    }
    return speaker.isBusy() == expected;
}

bool waitForFailure(bool expected, uint32_t timeout_ms)
{
    const uint32_t started_at = millis();
    while (millis() - started_at < timeout_ms) {
        if (speaker.lastPlaybackFailed() == expected) {
            return true;
        }
        delay(10);
    }
    return speaker.lastPlaybackFailed() == expected;
}

bool waitForPinLevel(uint8_t pin, uint8_t expected, uint32_t timeout_ms)
{
    const uint32_t started_at = millis();
    while (millis() - started_at < timeout_ms) {
        if (digitalRead(pin) == expected) {
            return true;
        }
        delay(10);
    }
    return digitalRead(pin) == expected;
}

bool retryStop(uint32_t timeout_ms = 250)
{
    const uint32_t started_at = millis();
    do {
        if (speaker.stop()) {
            return true;
        }
        delay(1);
    } while (millis() - started_at < timeout_ms);
    return false;
}

bool stopAndWait()
{
    if (!retryStop()) {
        return false;
    }
    return waitForState(SPEAKER_MODE_SILENT, nullptr, STATE_TIMEOUT_MS) &&
           waitForBusy(false, STATE_TIMEOUT_MS);
}

bool writeCorruptFixture()
{
    if (!LittleFS.begin()) {
        return false;
    }

    LittleFS.remove(AUDIO_CORRUPT);
    File file = LittleFS.open(AUDIO_CORRUPT, "w");
    if (!file) {
        LittleFS.end();
        return false;
    }

    const uint8_t invalid_wav[] = {
        'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'
    };
    const bool written =
        file.write(invalid_wav, sizeof(invalid_wav)) == sizeof(invalid_wav);
    file.close();
    LittleFS.end();
    return written;
}

bool prepareCorruptFixture()
{
    const bool restart_speaker = speaker.isBegun();
    if (restart_speaker) {
        speaker.end();
    }

    const bool written = writeCorruptFixture();
    const bool restarted = !restart_speaker || speaker.begin();
    return written && restarted;
}

bool removeCorruptFixture(bool restart_speaker)
{
    speaker.end();
    bool removed = false;
    if (LittleFS.begin()) {
        removed = !LittleFS.exists(AUDIO_CORRUPT) ||
                  LittleFS.remove(AUDIO_CORRUPT);
        LittleFS.end();
    }

    const bool restarted = !restart_speaker || speaker.begin();
    return removed && restarted;
}

void printState()
{
    const char* path = speaker.currentAudioPath();
    Serial.print("[STATE] begun=");
    Serial.print(speaker.isBegun());
    Serial.print(" task_running=");
    Serial.print(speaker.isTaskRunning());
    Serial.print(" mode=");
    Serial.print(modeName(speaker.currentMode()));
    Serial.print(" path=");
    Serial.print(path == nullptr ? "<null>" : path);
    Serial.print(" busy=");
    Serial.print(speaker.isBusy());
    Serial.print(" volume=");
    Serial.print(speaker.volume());
    Serial.print(" failed=");
    Serial.print(speaker.lastPlaybackFailed());
    Serial.print(" shutdown_pin=");
    Serial.println(digitalRead(SPEAKER_SHUTDOWN_PIN));
}

void testLifecycle()
{
    Serial.println("\n=== Lifecycle ===");
    stopAndWait();

    speaker.end();
    speaker.end();
    delay(100);
    reportCheck(!speaker.isBegun(), "end() is idempotent and clears begun state");
    reportCheck(!speaker.isTaskRunning(), "end() stops the speaker task");
    reportCheck(speaker.currentMode() == SPEAKER_MODE_SILENT,
                "end() leaves Silent mode");
    reportCheck(speaker.currentAudioPath() == nullptr,
                "end() clears current audio path");
    reportCheck(digitalRead(SPEAKER_SHUTDOWN_PIN) == LOW,
                "end() disables the speaker amplifier");
    reportCheck(!speaker.playOnce(AUDIO_BOOT),
                "playOnce() is rejected after end()");
    reportCheck(!speaker.playLoop(AUDIO_BEEP_SLOW),
                "playLoop() is rejected after end()");
    reportCheck(!speaker.stop(), "stop() is rejected after end()");

    const bool first_begin = speaker.begin();
    const bool second_begin = speaker.begin();
    delay(100);
    reportCheck(first_begin && second_begin, "begin() is idempotent");
    reportCheck(speaker.isBegun(), "begin() sets begun state");
    reportCheck(speaker.isTaskRunning(), "begin() starts the speaker task");
    reportCheck(speaker.currentMode() == SPEAKER_MODE_SILENT,
                "begin() starts in Silent mode");
    reportCheck(speaker.currentAudioPath() == nullptr,
                "begin() starts with no current path");
    reportCheck(speaker.volume() == SPEAKER_DEFAULT_VOLUME,
                "begin() restores default volume");
    reportCheck(!speaker.lastPlaybackFailed(),
                "begin() clears playback failure state");
    reportCheck(digitalRead(SPEAKER_SHUTDOWN_PIN) == LOW,
                "begin() keeps amplifier disabled while idle");

    bool stress_passed = true;
    for (uint8_t i = 0; i < 10; i++) {
        speaker.end();
        const bool stopped = !speaker.isBegun() &&
                             !speaker.isTaskRunning() &&
                             digitalRead(SPEAKER_SHUTDOWN_PIN) == LOW;
        const bool started = speaker.begin() &&
                             speaker.isBegun() &&
                             speaker.isTaskRunning();
        stress_passed = stress_passed && stopped && started;
        Serial.print("[STRESS] lifecycle round ");
        Serial.print(i + 1);
        Serial.println(stopped && started ? " PASS" : " FAIL");
    }
    reportCheck(stress_passed, "10 repeated end()/begin() rounds pass");

    reportObservation("beep starts briefly, then end() stops it without residual sound");
    const bool loop_accepted = speaker.playLoop(AUDIO_BEEP_SLOW);
    const bool loop_started = waitForState(
        SPEAKER_MODE_LOOP, AUDIO_BEEP_SLOW, STATE_TIMEOUT_MS);
    delay(500);
    speaker.end();
    delay(100);
    reportCheck(loop_accepted && loop_started,
                "active loop starts before lifecycle shutdown test");
    reportCheck(!speaker.isTaskRunning() &&
                digitalRead(SPEAKER_SHUTDOWN_PIN) == LOW,
                "end() during playback stops task and amplifier");
    reportCheck(speaker.begin(), "begin() succeeds after active playback shutdown");
}

void testOnce()
{
    Serial.println("\n=== playOnce ===");
    stopAndWait();
    speaker.setVolume(50);

    reportObservation("boot.wav plays once, completely and without obvious pop noise");
    const bool accepted = speaker.playOnce(AUDIO_BOOT);
    const bool busy_on_return = speaker.isBusy();
    reportCheck(accepted, "playOnce(boot) is accepted");
    reportCheck(accepted && busy_on_return,
                "accepted once becomes busy immediately");
    reportCheck(accepted && waitForState(
                    SPEAKER_MODE_PLAY_ONCE, AUDIO_BOOT, STATE_TIMEOUT_MS),
                "task enters PLAY_ONCE with boot path");
    reportCheck(waitForPinLevel(
                    SPEAKER_SHUTDOWN_PIN, HIGH, STATE_TIMEOUT_MS),
                "amplifier is enabled during once playback");
    reportCheck(waitForBusy(false, ONCE_TIMEOUT_MS),
                "busy clears after boot finishes");
    reportCheck(waitForState(SPEAKER_MODE_SILENT, nullptr, STATE_TIMEOUT_MS),
                "once completion returns to Silent with no path");
    reportCheck(!speaker.lastPlaybackFailed(),
                "successful once does not set failure state");

    delay(IDLE_SHUTDOWN_WAIT_MS);
    reportCheck(digitalRead(SPEAKER_SHUTDOWN_PIN) == LOW,
                "idle timeout disables amplifier after once");
}

void testLoop()
{
    Serial.println("\n=== playLoop ===");
    stopAndWait();
    speaker.setVolume(50);

    reportObservation("beep_slow.wav repeats for more than two complete cycles");
    const bool accepted = speaker.playLoop(AUDIO_BEEP_SLOW);
    reportCheck(accepted, "playLoop(beep_slow) is accepted");
    reportCheck(accepted && !speaker.isBusy(), "loop does not count as busy");
    reportCheck(accepted && waitForState(
                    SPEAKER_MODE_LOOP, AUDIO_BEEP_SLOW, STATE_TIMEOUT_MS),
                "task enters LOOP with beep_slow path");
    delay(LOOP_OBSERVE_MS);
    reportCheck(speaker.currentMode() == SPEAKER_MODE_LOOP &&
                pathEquals(AUDIO_BEEP_SLOW),
                "loop remains active after two file durations");
    reportCheck(!speaker.lastPlaybackFailed(),
                "valid loop does not set failure state");
    reportCheck(stopAndWait(), "loop can be stopped cleanly");
}

void testInterruptAndRestore()
{
    Serial.println("\n=== loop -> once -> loop ===");
    stopAndWait();
    speaker.setVolume(50);

    reportObservation("beep loop is interrupted by success tone, then the same beep loop resumes");
    const bool loop_accepted = speaker.playLoop(AUDIO_BEEP_SLOW);
    const bool loop_started = waitForState(
        SPEAKER_MODE_LOOP, AUDIO_BEEP_SLOW, STATE_TIMEOUT_MS);
    delay(900);

    const bool once_accepted = speaker.playOnce(AUDIO_TONE_SUCCESS);
    reportCheck(loop_accepted && loop_started,
                "base loop starts before once interruption");
    reportCheck(once_accepted, "success tone once is accepted during loop");
    reportCheck(once_accepted && speaker.isBusy(),
                "interrupting once becomes busy immediately");
    reportCheck(once_accepted && waitForState(
                    SPEAKER_MODE_PLAY_ONCE,
                    AUDIO_TONE_SUCCESS,
                    STATE_TIMEOUT_MS),
                "task switches from loop to success tone once");
    reportCheck(waitForState(
                    SPEAKER_MODE_LOOP,
                    AUDIO_BEEP_SLOW,
                    ONCE_TIMEOUT_MS),
                "same loop is restored after once completion");
    reportCheck(!speaker.isBusy(), "restored loop is not busy");
    delay(3800);
    reportCheck(speaker.currentMode() == SPEAKER_MODE_LOOP &&
                pathEquals(AUDIO_BEEP_SLOW),
                "restored loop remains active");
    reportCheck(stopAndWait(), "restored loop stops cleanly");
}

void testLatestOnceWins()
{
    Serial.println("\n=== once A -> once B ===");
    stopAndWait();
    speaker.setVolume(50);

    reportObservation("boot is cut short and only the newer success tone completes");
    const bool first_accepted = speaker.playOnce(AUDIO_BOOT);
    const bool first_started = waitForState(
        SPEAKER_MODE_PLAY_ONCE, AUDIO_BOOT, STATE_TIMEOUT_MS);
    delay(100);
    const bool second_accepted = speaker.playOnce(AUDIO_TONE_SUCCESS);

    reportCheck(first_accepted && first_started,
                "first once starts before replacement");
    reportCheck(second_accepted, "newer once is accepted");
    reportCheck(second_accepted && waitForState(
                    SPEAKER_MODE_PLAY_ONCE,
                    AUDIO_TONE_SUCCESS,
                    STATE_TIMEOUT_MS),
                "newer once replaces current path");
    reportCheck(waitForBusy(false, ONCE_TIMEOUT_MS),
                "busy clears after newer once finishes");
    reportCheck(waitForState(SPEAKER_MODE_SILENT, nullptr, STATE_TIMEOUT_MS),
                "newer once completion returns to Silent");
    reportCheck(!speaker.lastPlaybackFailed(),
                "once replacement does not create failure state");
}

void testStopNoRestore()
{
    Serial.println("\n=== stop ===");
    stopAndWait();
    speaker.setVolume(50);

    reportObservation("beep is interrupted by boot, then stop keeps the speaker silent");
    const bool loop_accepted = speaker.playLoop(AUDIO_BEEP_SLOW);
    const bool loop_started = waitForState(
        SPEAKER_MODE_LOOP, AUDIO_BEEP_SLOW, STATE_TIMEOUT_MS);
    delay(800);
    const bool once_accepted = speaker.playOnce(AUDIO_BOOT);
    const bool busy_before_stop = speaker.isBusy();
    const bool once_started = waitForState(
        SPEAKER_MODE_PLAY_ONCE, AUDIO_BOOT, STATE_TIMEOUT_MS);
    delay(100);
    const bool stop_accepted = speaker.stop();
    const bool busy_after_stop = speaker.isBusy();

    reportCheck(loop_accepted && loop_started,
                "loop starts before stop test");
    reportCheck(once_accepted && busy_before_stop && once_started,
                "once interrupts loop and creates a pending loop restore");
    reportCheck(stop_accepted, "stop() is accepted during interrupting once");
    reportCheck(stop_accepted && !busy_after_stop,
                "stop() clears busy synchronously");
    reportCheck(waitForState(SPEAKER_MODE_SILENT, nullptr, STATE_TIMEOUT_MS),
                "stop() changes actual state to Silent");
    delay(STOP_NO_RESTORE_MS);
    reportCheck(speaker.currentMode() == SPEAKER_MODE_SILENT &&
                speaker.currentAudioPath() == nullptr,
                "old loop is not restored after stop()");
    reportCheck(!speaker.lastPlaybackFailed(),
                "normal stop does not set failure state");
}

void testFailures()
{
    Serial.println("\n=== Invalid paths and WAV ===");
    stopAndWait();

    reportCheck(!speaker.playOnce(nullptr),
                "playOnce(nullptr) is rejected");
    reportCheck(!speaker.playOnce(""),
                "playOnce(empty path) is rejected");
    reportCheck(speaker.currentMode() == SPEAKER_MODE_SILENT &&
                speaker.currentAudioPath() == nullptr,
                "rejected paths do not change current state");

    const bool fixture_ready = prepareCorruptFixture();
    reportCheck(fixture_ready, "runtime corrupt WAV fixture is prepared");

    const bool missing_accepted = speaker.playOnce(AUDIO_MISSING);
    const bool missing_busy_on_return = speaker.isBusy();
    reportCheck(missing_accepted, "missing path command is accepted for task validation");
    reportCheck(missing_accepted && missing_busy_on_return,
                "missing once is busy while pending");
    reportCheck(waitForFailure(true, STATE_TIMEOUT_MS),
                "missing file sets playback failure");
    reportCheck(waitForState(SPEAKER_MODE_SILENT, nullptr, STATE_TIMEOUT_MS) &&
                !speaker.isBusy(),
                "missing file failure returns to Silent and clears busy");
    delay(800);
    reportCheck(speaker.lastPlaybackFailed() &&
                speaker.currentMode() == SPEAKER_MODE_SILENT,
                "missing file is not retried automatically");

    reportCheck(speaker.stop(), "stop() is accepted after failure");
    reportCheck(speaker.lastPlaybackFailed(),
                "stop() does not clear historical failure state");

    const bool valid_accepted = speaker.playOnce(AUDIO_TONE_SUCCESS);
    reportCheck(valid_accepted && !speaker.lastPlaybackFailed(),
                "new valid playback command clears failure state");
    stopAndWait();

    const bool corrupt_accepted = speaker.playOnce(AUDIO_CORRUPT);
    reportCheck(corrupt_accepted, "corrupt WAV command is accepted for parsing");
    reportCheck(waitForFailure(true, STATE_TIMEOUT_MS),
                "corrupt WAV sets playback failure");
    reportCheck(waitForState(SPEAKER_MODE_SILENT, nullptr, STATE_TIMEOUT_MS) &&
                !speaker.isBusy(),
                "corrupt WAV failure returns to Silent and clears busy");

    const bool recovery_accepted = speaker.playOnce(AUDIO_TONE_SUCCESS);
    reportCheck(recovery_accepted && !speaker.lastPlaybackFailed(),
                "valid command clears corrupt WAV failure state");
    reportCheck(waitForBusy(false, ONCE_TIMEOUT_MS),
                "valid playback completes after prior failures");
}

void playToneAtVolume(uint8_t volume, const char* observation)
{
    reportCheck(speaker.setVolume(volume), "volume update is accepted");
    reportCheck(speaker.volume() == volume, "volume getter returns requested level");
    reportObservation(observation);
    const bool accepted = speaker.playOnce(AUDIO_TONE_SUCCESS);
    reportCheck(accepted, "volume test tone is accepted");
    reportCheck(accepted && waitForBusy(false, ONCE_TIMEOUT_MS),
                "volume test tone completes");
}

void testVolume()
{
    Serial.println("\n=== Volume ===");
    stopAndWait();

    playToneAtVolume(0, "0%: success tone is silent");
    playToneAtVolume(50, "50%: success tone is clearly audible at moderate level");
    playToneAtVolume(100, "100%: one short success tone is louder without distortion");

    reportCheck(speaker.setVolume(255),
                "out-of-range volume request is accepted");
    reportCheck(speaker.volume() == SPEAKER_MAX_VOLUME,
                "volume 255 is clamped to SPEAKER_MAX_VOLUME");

    speaker.setVolume(50);
    reportObservation("during beep loop: 50% audible, 0% silent, then 50% audible again");
    const bool loop_accepted = speaker.playLoop(AUDIO_BEEP_SLOW);
    const bool loop_started = waitForState(
        SPEAKER_MODE_LOOP, AUDIO_BEEP_SLOW, STATE_TIMEOUT_MS);
    delay(1000);
    const bool mute_accepted = speaker.setVolume(0);
    reportCheck(mute_accepted && speaker.volume() == 0,
                "live mute is accepted and reported as 0%");
    delay(1100);
    const bool restore_accepted = speaker.setVolume(50);
    reportCheck(restore_accepted && speaker.volume() == 50,
                "live volume restore is accepted and reported as 50%");
    delay(1100);

    reportCheck(loop_accepted && loop_started,
                "loop starts for live volume update test");
    reportCheck(speaker.currentMode() == SPEAKER_MODE_LOOP &&
                pathEquals(AUDIO_BEEP_SLOW),
                "volume updates do not replace loop command");
    reportCheck(stopAndWait(), "volume test loop stops cleanly");
    reportCheck(speaker.setVolume(SPEAKER_DEFAULT_VOLUME),
                "default volume is restored after volume tests");
}

void testRapidOverwrite()
{
    Serial.println("\n=== Rapid overwrite ===");
    stopAndWait();
    speaker.setVolume(50);

    uint16_t accepted = 0;
    uint16_t rejected = 0;
    for (uint16_t i = 0; i < RAPID_COMMAND_COUNT; i++) {
        bool command_accepted = false;
        switch (i % 3) {
        case 0:
            command_accepted = speaker.playOnce(AUDIO_BOOT);
            break;
        case 1:
            command_accepted = speaker.playLoop(AUDIO_BEEP_SLOW);
            break;
        default:
            command_accepted = speaker.stop();
            break;
        }

        if (command_accepted) {
            accepted++;
        } else {
            rejected++;
        }
    }

    Serial.print("[STRESS] accepted=");
    Serial.print(accepted);
    Serial.print(" rejected_due_to_nonblocking_lock=");
    Serial.println(rejected);

    reportCheck(accepted > 0, "rapid stress accepts at least one command");
    reportCheck(retryStop(), "final stop is eventually accepted after stress");
    reportCheck(waitForState(SPEAKER_MODE_SILENT, nullptr, STATE_TIMEOUT_MS),
                "latest accepted stop determines final Silent state");
    reportCheck(!speaker.isBusy(), "rapid stress finishes without busy residue");
    reportCheck(speaker.isTaskRunning(),
                "speaker task remains running after rapid stress");
    reportCheck(!speaker.lastPlaybackFailed(),
                "rapid valid commands do not create playback failure");
    reportCheck(waitForPinLevel(
                    SPEAKER_SHUTDOWN_PIN, LOW, IDLE_SHUTDOWN_WAIT_MS),
                "amplifier is disabled after rapid stress stop");
}

void printMenu()
{
    Serial.println("\nSpeaker production-controller hardware test");
    Serial.println("  a: run all tests");
    Serial.println("  1: playOnce boot");
    Serial.println("  2: playLoop beep_slow");
    Serial.println("  3: loop interrupted by once, then restored");
    Serial.println("  4: newer once replaces older once");
    Serial.println("  5: stop prevents old loop restore");
    Serial.println("  6: invalid path and corrupt WAV");
    Serial.println("  7: volume 0/50/100 and live update");
    Serial.println("  8: rapid once/loop/stop overwrite");
    Serial.println("  9: lifecycle and 10-round stress");
    Serial.println("  p: print current state");
    Serial.println("  s: stop playback");
    Serial.println("  x: remove corrupt fixture and end controller");
    Serial.println("  h: print this menu");
}

void runAllTests()
{
    passed_checks = 0;
    total_checks = 0;

    Serial.println("\n========== SPEAKER TEST START ==========");
    reportCheck(prepareCorruptFixture(),
                "runtime corrupt WAV fixture is ready for full test");
    testLifecycle();
    testOnce();
    testLoop();
    testInterruptAndRestore();
    testLatestOnceWins();
    testStopNoRestore();
    testFailures();
    testVolume();
    testRapidOverwrite();
    stopAndWait();

    Serial.println("\n========== SPEAKER TEST SUMMARY ==========");
    Serial.print("[AUTOMATED] ");
    Serial.print(passed_checks);
    Serial.print('/');
    Serial.print(total_checks);
    Serial.println(" checks passed");
    Serial.println("[REQUIRED] Confirm every [OBSERVE] item on the physical speaker/video.");
    printState();
    Serial.println("==========================================");
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(1500);

    Serial.println("\nBooting Speaker production-controller hardware test...");
    const bool fixture_ready = writeCorruptFixture();
    const bool begun = speaker.begin();
    if (!fixture_ready) {
        Serial.println("[FATAL] Failed to prepare runtime corrupt WAV fixture");
    }
    if (!begun) {
        Serial.println("[FATAL] SpeakerController::begin() failed");
        return;
    }

    printMenu();
    printState();
}

void loop()
{
    if (!Serial.available()) {
        delay(20);
        return;
    }

    const char command = static_cast<char>(Serial.read());
    switch (command) {
    case 'a':
    case 'A':
        runAllTests();
        break;
    case '1':
        testOnce();
        break;
    case '2':
        testLoop();
        break;
    case '3':
        testInterruptAndRestore();
        break;
    case '4':
        testLatestOnceWins();
        break;
    case '5':
        testStopNoRestore();
        break;
    case '6':
        testFailures();
        break;
    case '7':
        testVolume();
        break;
    case '8':
        testRapidOverwrite();
        break;
    case '9':
        testLifecycle();
        break;
    case 'p':
    case 'P':
        printState();
        break;
    case 's':
    case 'S':
        reportCheck(stopAndWait(), "manual stop reaches Silent state");
        break;
    case 'x':
    case 'X':
        reportCheck(removeCorruptFixture(false),
                    "corrupt fixture removed and controller ended");
        printState();
        break;
    case 'h':
    case 'H':
        printMenu();
        break;
    case '\r':
    case '\n':
        break;
    default:
        Serial.println("[WARN] Unknown command. Enter h for help.");
        break;
    }
}
