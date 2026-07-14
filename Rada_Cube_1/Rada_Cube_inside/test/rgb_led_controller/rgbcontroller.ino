#include "../../src/rgb_led_controller.h"

namespace {

RgbLedController rgb_led;
uint16_t passed_checks = 0;
uint16_t total_checks = 0;

void reportCheck(bool passed, const char* description)
{
    total_checks++;
    if (passed) {
        passed_checks++;
    }

    Serial.print(passed ? "[PASS] " : "[FAIL] ");
    Serial.println(description);
}

void reportObservation(const char* description, uint32_t duration_ms)
{
    Serial.print("[OBSERVE] ");
    Serial.print(description);
    Serial.print(" (");
    Serial.print(duration_ms);
    Serial.println(" ms)");
    delay(duration_ms);
}

bool waitForBusy(bool expected, uint32_t timeout_ms)
{
    const uint32_t started_at = millis();
    while (millis() - started_at < timeout_ms) {
        if (rgb_led.isBusy() == expected) {
            return true;
        }
        delay(10);
    }
    return rgb_led.isBusy() == expected;
}

void separateVisualSteps()
{
    rgb_led.off();
    delay(250);
}

void testLifecycle()
{
    Serial.println("\n=== Lifecycle ===");

    rgb_led.end();
    rgb_led.end();
    delay(100);
    reportCheck(!rgb_led.isBegun(), "end() is idempotent and clears begun state");
    reportCheck(!rgb_led.isTaskRunning(), "end() stops the RGB task");
    reportCheck(digitalRead(RGB_LED_PWR_PIN) == LOW,
                "end() disables RGB power");
    reportCheck(!rgb_led.solid(RGB_COLOR_RED),
                "visual commands are rejected after end()");

    const bool first_begin = rgb_led.begin();
    const bool second_begin = rgb_led.begin();
    delay(100);
    reportCheck(first_begin && second_begin, "begin() is idempotent");
    reportCheck(rgb_led.isBegun(), "begin() sets begun state");
    reportCheck(rgb_led.isTaskRunning(), "begin() starts the RGB task");
    reportCheck(digitalRead(RGB_LED_PWR_PIN) == HIGH,
                "begin() enables RGB power");
}

void testApiContract()
{
    Serial.println("\n=== API contract ===");

    reportCheck(rgb_led.setBrightness(255),
                "setBrightness() accepts an out-of-range request");
    reportCheck(rgb_led.brightness() == RGB_LED_MAX_BRIGHTNESS,
                "brightness is clamped to RGB_LED_MAX_BRIGHTNESS");
    reportCheck(rgb_led.setBrightness(RGB_LED_DEFAULT_BRIGHTNESS),
                "default brightness is accepted");

    reportCheck(!rgb_led.solid(
                    RGB_COLOR_RED,
                    static_cast<LedZone>(LED_ZONE_RIGHT + 1)),
                "solid() rejects an invalid zone");
    reportCheck(!rgb_led.blink(
                    RGB_COLOR_RED,
                    static_cast<RgbEffectSpeed>(RGB_EFFECT_SPEED_FAST + 1)),
                "blink() rejects an invalid speed");
    reportCheck(!rgb_led.breathe(
                    RGB_COLOR_RED,
                    RGB_EFFECT_SPEED_MEDIUM,
                    static_cast<LedZone>(LED_ZONE_RIGHT + 1)),
                "breathe() rejects an invalid zone");
    reportCheck(!rgb_led.playAnimation(
                    static_cast<RgbAnimation>(RGB_ANIMATION_FADE_OUT + 1)),
                "playAnimation() rejects an invalid animation");

    reportCheck(rgb_led.solid(RGB_COLOR_RED),
                "a visual command can be queued before brightness update");
    reportCheck(rgb_led.setBrightness(255),
                "brightness update merges with a pending visual command");
    delay(100);
    reportCheck(rgb_led.brightness() == RGB_LED_MAX_BRIGHTNESS,
                "merged brightness remains clamped");
    rgb_led.setBrightness(RGB_LED_DEFAULT_BRIGHTNESS);
    rgb_led.off();
}

void showZone(LedZone zone, const char* expectation)
{
    reportCheck(rgb_led.solid(RGB_COLOR_WHITE, zone), expectation);
    reportObservation(expectation, 1400);
    separateVisualSteps();
}

void testZones()
{
    Serial.println("\n=== Zones ===");
    showZone(LED_ZONE_ALL, "LED_ZONE_ALL: all four LEDs are on");
    showZone(LED_ZONE_ONLY_LEFT,
             "LED_ZONE_ONLY_LEFT: only the left LED is on");
    showZone(LED_ZONE_ONLY_RIGHT,
             "LED_ZONE_ONLY_RIGHT: only the right LED is on");
    showZone(LED_ZONE_ONLY_SIDES,
             "LED_ZONE_ONLY_SIDES: left and right LEDs are on; top and bottom are off");
    showZone(LED_ZONE_LEFT,
             "LED_ZONE_LEFT: left, top and bottom LEDs are on");
    showZone(LED_ZONE_RIGHT,
             "LED_ZONE_RIGHT: right, top and bottom LEDs are on");

    reportCheck(rgb_led.solid(RGB_COLOR_WHITE, LED_ZONE_NONE),
                "LED_ZONE_NONE command is accepted");
    reportObservation("LED_ZONE_NONE: all four LEDs are off", 1000);
    reportCheck(rgb_led.off(), "off() command is accepted");
    reportObservation("off(): all four LEDs remain off", 1000);
}

void showBlink(RgbEffectSpeed speed,
               const char* expectation,
               uint32_t duration_ms)
{
    reportCheck(rgb_led.blink(RGB_COLOR_BLUE, speed, LED_ZONE_ALL), expectation);
    reportObservation(expectation, duration_ms);
    separateVisualSteps();
}

void testBlink()
{
    Serial.println("\n=== Blink speeds ===");
    showBlink(RGB_EFFECT_SPEED_SLOW,
              "blink slow: about 1000 ms per cycle", 3200);
    showBlink(RGB_EFFECT_SPEED_MEDIUM,
              "blink medium: about 500 ms per cycle", 2200);
    showBlink(RGB_EFFECT_SPEED_FAST,
              "blink fast: about 200 ms per cycle", 1400);
}

void showBreathe(RgbEffectSpeed speed,
                 const char* expectation,
                 uint32_t duration_ms)
{
    reportCheck(rgb_led.breathe(RGB_COLOR_GREEN, speed, LED_ZONE_ALL),
                expectation);
    reportObservation(expectation, duration_ms);
    separateVisualSteps();
}

void testBreathe()
{
    Serial.println("\n=== Breathe speeds ===");
    showBreathe(RGB_EFFECT_SPEED_SLOW,
                "breathe slow: about 2400 ms per cycle", 5200);
    showBreathe(RGB_EFFECT_SPEED_MEDIUM,
                "breathe medium: about 1600 ms per cycle", 3600);
    showBreathe(RGB_EFFECT_SPEED_FAST,
                "breathe fast: about 900 ms per cycle", 2200);
}

void testAnimation(RgbAnimation animation,
                   const char* expectation,
                   uint32_t idle_timeout_ms)
{
    const bool accepted = rgb_led.playAnimation(animation);
    reportCheck(accepted, expectation);
    reportCheck(accepted && waitForBusy(true, 200),
                "isBusy() becomes true while animation is pending or running");
    Serial.print("[OBSERVE] ");
    Serial.println(expectation);
    reportCheck(accepted && waitForBusy(false, idle_timeout_ms),
                "isBusy() becomes false after animation finishes");
    reportObservation("animation completion leaves all LEDs off", 500);
}

void testAnimations()
{
    Serial.println("\n=== Animations ===");
    testAnimation(
        RGB_ANIMATION_CHASE_CLOCKWISE,
        "clockwise green chase: BOTTOM -> LEFT -> TOP -> RIGHT, about 1 second",
        1800);
    testAnimation(
        RGB_ANIMATION_CHASE_COUNTERCLOCKWISE,
        "counterclockwise green chase: BOTTOM -> RIGHT -> TOP -> LEFT, about 1 second",
        1800);
    testAnimation(
        RGB_ANIMATION_FLASH,
        "green flash: all four LEDs flash three times at about 180 ms per cycle",
        1400);
    testAnimation(
        RGB_ANIMATION_FADE_OUT,
        "soft amber fade-out: RIGHT -> TOP -> LEFT -> BOTTOM, about 1 second",
        1800);

    reportCheck(rgb_led.playAnimation(RGB_ANIMATION_NONE),
                "RGB_ANIMATION_NONE is accepted as off()");
    delay(100);
    reportCheck(!rgb_led.isBusy(),
                "RGB_ANIMATION_NONE does not leave busy state");
    reportObservation("RGB_ANIMATION_NONE leaves all LEDs off", 500);

    reportCheck(rgb_led.playAnimation(RGB_ANIMATION_FADE_OUT),
                "interrupt test animation is accepted");
    reportCheck(waitForBusy(true, 200),
                "interrupt test enters busy state");
    delay(180);
    reportCheck(rgb_led.solid(RGB_COLOR_BLUE, LED_ZONE_ALL),
                "continuous effect interrupts an animation");
    reportCheck(waitForBusy(false, 300),
                "animation interruption clears busy state");
    reportObservation("interrupted animation is replaced by solid blue", 1000);
    separateVisualSteps();
}

void testRapidOverwrite()
{
    Serial.println("\n=== Rapid overwrite ===");

    uint16_t accepted = 0;
    constexpr uint16_t COMMAND_COUNT = 600;
    for (uint16_t i = 0; i < COMMAND_COUNT; i++) {
        bool command_accepted = false;
        switch (i % 6) {
        case 0:
            command_accepted = rgb_led.solid(RGB_COLOR_RED, LED_ZONE_ONLY_LEFT);
            break;
        case 1:
            command_accepted = rgb_led.blink(
                RGB_COLOR_GREEN, RGB_EFFECT_SPEED_FAST, LED_ZONE_ONLY_RIGHT);
            break;
        case 2:
            command_accepted = rgb_led.breathe(
                RGB_COLOR_BLUE, RGB_EFFECT_SPEED_FAST, LED_ZONE_ONLY_SIDES);
            break;
        case 3:
            command_accepted = rgb_led.playAnimation(RGB_ANIMATION_FLASH);
            break;
        case 4:
            command_accepted = rgb_led.setBrightness(
                static_cast<uint8_t>(i % (RGB_LED_MAX_BRIGHTNESS + 1)));
            break;
        default:
            command_accepted = rgb_led.off();
            break;
        }
        if (command_accepted) {
            accepted++;
        }
    }

    reportCheck(accepted == COMMAND_COUNT,
                "all rapid overwrite commands are accepted");
    reportCheck(rgb_led.solid(RGB_COLOR_GREEN, LED_ZONE_ALL),
                "final command after rapid overwrite is accepted");
    delay(300);
    reportCheck(rgb_led.isTaskRunning(),
                "RGB task remains running after rapid overwrite");
    reportCheck(!rgb_led.isBusy(),
                "final continuous effect clears animation busy state");
    reportObservation("final state is solid green on all four LEDs", 1200);
    rgb_led.setBrightness(RGB_LED_DEFAULT_BRIGHTNESS);
    separateVisualSteps();
}

void printState()
{
    Serial.print("[STATE] begun=");
    Serial.print(rgb_led.isBegun());
    Serial.print(" task_running=");
    Serial.print(rgb_led.isTaskRunning());
    Serial.print(" busy=");
    Serial.print(rgb_led.isBusy());
    Serial.print(" brightness=");
    Serial.print(rgb_led.brightness());
    Serial.print(" power_pin=");
    Serial.println(digitalRead(RGB_LED_PWR_PIN));
}

void printMenu()
{
    Serial.println("\nRGB production-controller hardware test");
    Serial.println("  a: run all tests");
    Serial.println("  z: test LED zones");
    Serial.println("  b: test blink speeds");
    Serial.println("  r: test breathe speeds");
    Serial.println("  m: test animations and interruption");
    Serial.println("  q: test rapid command overwrite");
    Serial.println("  l: test repeated begin/end and RGB power");
    Serial.println("  c: test API contract and invalid values");
    Serial.println("  p: print current state");
    Serial.println("  o: turn all LEDs off");
    Serial.println("  h: print this menu");
}

void runAllTests()
{
    passed_checks = 0;
    total_checks = 0;

    Serial.println("\n========== RGB TEST START ==========");
    testLifecycle();
    testApiContract();
    testZones();
    testBlink();
    testBreathe();
    testAnimations();
    testRapidOverwrite();
    rgb_led.off();

    Serial.println("\n========== RGB TEST SUMMARY ==========");
    Serial.print("[AUTOMATED] ");
    Serial.print(passed_checks);
    Serial.print('/');
    Serial.print(total_checks);
    Serial.println(" checks passed");
    Serial.println("[REQUIRED] Confirm every [OBSERVE] item on the physical device/video.");
    printState();
    Serial.println("======================================");
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(1500);

    Serial.println("\nBooting RGB production-controller hardware test...");
    if (!rgb_led.begin()) {
        Serial.println("[FATAL] RgbLedController::begin() failed");
        return;
    }

    rgb_led.off();
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
    case 'z':
    case 'Z':
        testZones();
        break;
    case 'b':
    case 'B':
        testBlink();
        break;
    case 'r':
    case 'R':
        testBreathe();
        break;
    case 'm':
    case 'M':
        testAnimations();
        break;
    case 'q':
    case 'Q':
        testRapidOverwrite();
        break;
    case 'l':
    case 'L':
        testLifecycle();
        break;
    case 'c':
    case 'C':
        testApiContract();
        break;
    case 'p':
    case 'P':
        printState();
        break;
    case 'o':
    case 'O':
        reportCheck(rgb_led.off(), "off() command is accepted");
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
