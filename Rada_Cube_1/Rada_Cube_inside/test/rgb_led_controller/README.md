# RgbLedController independent real machine test

This test directly compiles and calls the production `src/rgb_led_controller.h/.cpp` without copying the driver source code. The serial port assertion only verifies the API and status that can be observed by the software; the lighting area, animation direction and lighting effect rhythm must be confirmed with physical objects.

## Compile and burn

Perform the following steps in the `Rada_Cube_inside` directory:

1. Temporarily rename `src/main.ino` to `src/main.ino.old`.
2. Copy `test/rgb_led_controller/rgbcontroller_entry.ino` to `src/rgbcontroller.ino`.
3. Execute `pio run` and `pio run -t upload`.
4. Open the serial port at 115200 baud and enter `a` to run the complete test.
5. After the test, delete the temporary `src/rgbcontroller.ino` and restore `src/main.ino.old` to `src/main.ino`.

Do not commit the temporary `src` directory changes produced in steps 1, 2, and 5.

## Complete testing and life cycle stress testing

Entering `a` will run a complete test, including:

- One `begin()/end()` lifecycle check.
- API parameters, illegal enumeration and brightness clamp checks.
- All light areas and `off()` check.
- blink/breathe third speed check.
- Four limited animations, `isBusy()` and animation preemption checks.
- 600 quick command coverage checks.

At the end of the complete test, the serial port summary should show `54/54 checks passed`. Among them, `[PASS]` is the result of automatic checking by the software, and all `[OBSERVE]` items still need to be confirmed by physical objects or videos.

Enter `l` once to run only one round of life cycle testing: call `end()` twice to check begun, task, RGB power supply and command rejection status; then call `begin()` twice to check begun, task and RGB power supply restoration. It does not validate light zones, frequencies, or animations.

Enter ten consecutive `l`s:

```text
llllllllll
```

You can perform 10 consecutive rounds of life cycle stress testing, mainly to verify whether task, NeoPixel and RMT resources can be stably and repeatedly created and released. Each round should output 8 items of `[PASS]`, for a total of 80 items in ten rounds, and there must be no RMT error, insufficient memory, abnormal reset or stuck.

This can be done manually in PlatformIO as follows:

```powershell
pio device monitor -p COM6 -b 115200
```

Enter `llllllllll` directly after entering the serial port. If the serial port terminal used does not send characters immediately, press Enter after typing; the test program will ignore line breaks. After the test is completed, enter `p` and the expected status is:

```text
[STATE] begun=1 task_running=1 busy=0 brightness=64 power_pin=1
```

`begin()` will be called again at the end of the life cycle test, so the final task and RGB power supply remain on.

## Automatic check

- [x] Repeated calls of `begin()/end()` remain idempotent.
- [x] After `end()`, the task stops, the RGB power supply is turned off, and new lighting effect commands are rejected.
- [x] After `begin()`, the task runs and RGB power supply is turned on.
- [x] Brightness input clamped to `RGB_LED_MAX_BRIGHTNESS`.
- [x] The lighting effect command waiting to be executed is correctly merged with the subsequent brightness command.
- [x] Illegal zone, speed, animation returns `false`.
- [x] Four limited animations run with `isBusy()` as `true` and return to `false` after completion.
- [x] Clear busy in time after continuous lighting effect preempts animation.
- [x] All 600 quick coverage commands were accepted, the task kept running and the final state was correct.

The serial port summary must show that all `[AUTOMATED]` checks have passed; the occurrence of any `[FAIL]` cannot determine that the test has passed.

## Physical inspection

- [x] `LED_ZONE_ALL`: Four lights are on.
- [x] `LED_ZONE_ONLY_LEFT`: Only the left light is on.
- [x] `LED_ZONE_ONLY_RIGHT`: Only the right light is on.
- [x] `LED_ZONE_ONLY_SIDES`: The left and right lights are on, but the upper and lower lights are off.
- [x] `LED_ZONE_LEFT`: The left, upper, and lower lights are on.
- [x] `LED_ZONE_RIGHT`: The right light, upper light, and lower light are on.
- [x] `LED_ZONE_NONE` and `off()`: four lights are off.
- [x] blink slow, medium and fast three speeds are about 1000/500/200 ms period.
- [x] breathe The three gears of slow, medium and fast are about 2400/1600/900 ms period.
- [x] Clockwise ticker: `BOTTOM -> LEFT -> TOP -> RIGHT`, green, about 1 second.
- [x] Counterclockwise marquee: `BOTTOM -> RIGHT -> TOP -> LEFT`, green, about 1 second.
- [x] `FLASH`: The four lights flash green 3 times quickly.
- [x] `FADE_OUT`: Soft amber fade by pressing `RIGHT -> TOP -> LEFT -> BOTTOM`.
- [x] After the animation is preempted by the constant light, the four-light blue light will be displayed immediately and the old animation will not be restored.
- [x] After the quick overwrite is completed, four green lights are displayed, and there is no stuck, abnormal reset or flickering residue.
- [x] `end()` The last four lights go out and the RGB power supply is turned off.

## Current result

- Production environment compilation: Passed (2026-07-14, ESP32-C6 / Arduino 3.3.9).
- Firmware burning: passed (2026-07-14, ESP32-C6, COM6).
- Automatic serial port check: passed, 54/54 checks passed; additional 10 consecutive rounds of `end()/begin()` stress test passed.
- Physical observation: Passed, all light areas, frequencies, animations and preemption effects have been confirmed to be correct.

The first actual measurement found that the RMT channel of GPIO4 was not reattached after repeating `end()/begin()`. The production Controller has been changed to create and destroy NeoPixel instances independently for each life cycle. After the repair, RMT errors no longer occur in repeated life cycles and complete automated tests.
