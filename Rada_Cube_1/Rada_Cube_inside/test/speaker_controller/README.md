#SpeakerController independent real machine test

This test directly compiles and calls the production `src/speaker_controller.h/.cpp` without copying the driver source code. The software status is automatically asserted by the serial port; whether the audio is complete, looped, restored, muted, and whether there is popping sound must be confirmed with physical objects or videos.

## Audio source

The resource definition is subject to the following content in the warehouse root directory:

- `tts_to_wav/data`
- `tts_to_wav/音频与音效清单.md`

Test use:

- `/boot.wav`: 0.589 seconds, 16 kHz, mono, 16-bit PCM WAV.
- `/beep_slow.wav`: 3.201 seconds, 16 kHz, mono, 16-bit PCM WAV.
- `/tone_success_up.wav`: 0.328 seconds, 16 kHz, mono, 16-bit PCM WAV.

Only temporarily copy `tone_success_up.wav` before burning LittleFS and delete it after testing.

## Compile and burn

Execute in the `Rada_Cube_inside` directory:

1. Temporarily copy `tts_to_wav/data/tone_success_up.wav` to `Rada_Cube_inside/data`.
2. Execute `pio run -t uploadfs --upload-port COM6`.
3. Delete temporary `Rada_Cube_inside/data/tone_success_up.wav`.
4. Temporarily rename `src/main.ino` to `src/main.ino.old`.
5. Copy `test/speaker_controller/speakercontroller_entry.ino` to `src/speakercontroller.ino`.
6. Execute `pio run` and `pio run -t upload --upload-port COM6`.
7. Execute `pio device monitor -p COM6 -b 115200` and enter `a` to run the complete test.
8. Enter `x` at the end of the test to remove the runtime corrupted WAV and close the Controller.
9. Delete temporary `src/speakercontroller.ino` and restore `src/main.ino.old` to `src/main.ino`.

Submitting temporary changes produced in steps 1, 3, 4, 5, and 9 is prohibited.

If esptool prompts `No serial data received`, first press and hold BOOT on the board and click RESET.
Release BOOT again to put ESP32-C6 into download mode and then re-execute the upload command.

## Serial port menu

- `a`: full test.
- `1`: once; `2`: loop; `3`: once interrupts the loop and resumes it.
- `4`: continuous once; `5`: stop; `6`: abnormal path.
- `7`: volume; `8`: fast coverage; `9`: life cycle and 10 rounds of stress testing.
- `p`: status; `s`: stop; `x`: clean test files and end; `h`: help.

## Automatic check

- [x] `begin()/end()` idempotent, passed 10 rounds of stress testing.
- [x] Task, I2S output and amplifier exit correctly after end during playback.
- [x] once is busy immediately after acceptance, and returns to Silent after completion.
- [x] loop Regardless of busy, the loop continues and the path remains correct.
- [x] once interrupts the loop and resumes the same loop.
- [x] The new once overwrites the old once, and only the latest once completes normally.
- [x] Do not restore old loop after stop.
- [x] nullptr and empty paths are rejected without changing the playback state.
- [x] Non-existent file and corrupted WAV set failed state, went Silent and did not retry.
- [x] stop does not clear the failure status, new play command clears the failure status.
- [x] The volume 255 is clamped to 100, and the volume update during playback does not cover the loop.
- [x] No deadlocks, crashes, abnormal resets or busy residues after 600 fast once/loop/stops.

## Hearing test

After typing `a`, expect to hear audio in the following order:

0. Life cycle test: `beep_slow` starts playing for about 0.5 seconds and then is stopped by `end()`, and then there is no residual sound.
1. once test: `boot` plays completely from beginning to end, lasting about 0.589 seconds, and mutes after the end.
2. Loop test: `beep_slow` continuously loops for about 7 seconds, more than two complete cycles can be heard, and then stops.
3. Loop recovery test: play `beep_slow` first, then switch to `tone_success_up` of 0.328 seconds after about 0.9 seconds; after the prompt tone ends, `beep_slow` automatically resumes and continues the loop, and then stops.
4. once coverage test: `boot` is interrupted by `tone_success_up` about 0.1 seconds after it starts; only the new `tone_success_up` is allowed to complete completely, and the old `boot` is not allowed to be restored.
5. stop test: play `beep_slow` first, and then switch to `boot`; `stop()` is executed about 0.1 seconds after `boot` starts, and then remains silent for at least 4.2 seconds. The old `beep_slow` must not be restored.
6. Abnormal recovery test: neither non-existent files nor damaged WAV should produce valid sound; after the abnormal state is cleared, `tone_success_up` should be able to play it completely again.
7. Volume test: Play `tone_success_up` once each at 0%, 50%, and 100%. It is expected to be silent, moderate, and obviously louder respectively; then `beep_slow` changes from 50% (about 1 second) → 0% (about 1.1 seconds) → 50% (about 1.1 seconds) without interrupting the loop, and finally stops.
8. Fast coverage test: 600 times once/loop/stop During fast coverage, only short fragments may be heard; in the end, the sound must be kept silent, and it must not continue to play, freeze, or have abnormal residual sounds.

- [x] boot is played completely once, without obvious popping sound.
- [x] beep_slow plays continuously for more than two file periods.
- [x] Success tone After inserting a beep loop, the same beep loop will automatically resume.
- [x] The boot is interrupted when there is a continuous once, and only the success tone is completed.
- [x] Wait for more than one loop period after stop and still remain silent.
- [x] 0% quiet, 50% moderate, 100% louder with no noticeable distortion.
- [x] When loop is playing, 50%→0%→50% can mute and resume without interrupting the loop.
- [x] There is no residual sound after end during playback.

## Current result

- Production environment compilation: passed (2026-07-14, ESP32-C6 / Arduino 3.3.9, full clean compilation).
- LittleFS upload: passed (2026-07-14, COM6, hash verification passed after writing).
- Firmware burning: passed (2026-07-14, COM6, hash verification passed after writing).
- Automatic serial port check: passed, `100/100 checks passed`.
- 10 rounds of life cycle stress test: Pass, all 10 rounds are PASS.
- Test Cleanup: Passed, runtime corrupt WAV removed, Controller ended, production main and data restored.
- Physical hearing: Passed, complete playback, looping, interruption recovery, stop mute, volume change and no obvious popping or residual sound were all confirmed to be normal.
- Video evidence: After the test passes, it will be attached to the PR or team communication channel and will not be submitted to the warehouse.
