#include <Arduino.h>

#include "ble_wake_scanner.h"
#include "config.h"
#include "espnow.h"
#include "mac_match.h"
#include "outside_state_controller.h"
#include "pins.h"
#include "radar.h"
#include "sensor.h"

enum SysMode {
    UNPAIRED_MODE,
    PAIRED_MODE,
    WORK_MODE,
    TEST_MODE,
    CONFIG_MODE,
    FACTORY_RESET_MODE,
};

HardwareSerial& RadarSerial = Serial1;
RadarModule Radar;
LEDControler Led;
PowerManager Power;
BleWakeScanner BleScanner;
EspNowManager Espnow;
MacMatch Matcher(Espnow);
OutsideStateController State(BleScanner, Power, Espnow, Matcher, Radar);

static void runWorkWithLed(bool started_by_ble, uint32_t wake_session)
{
    Led.led_on();
    State.runWork(started_by_ble, wake_session);
    Led.led_off();
}

static uint32_t waitButtonRelease()
{
    const uint32_t started = millis();
    bool long_press_indicated = false;
    Led.led_on();

    while (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
           digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED) {
        if (!long_press_indicated && millis() - started > BUTTON_LONG_PRESS_MS) {
            Led.led_off();
            long_press_indicated = true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    Led.led_off();
    const uint32_t held_ms = millis() - started;
    ESP_LOGI(MAIN_TAG, "Button hold: %lu ms", static_cast<unsigned long>(held_ms));
    return held_ms;
}

static SysMode detectButtonMode(WakeupSource source)
{
    const uint32_t held_ms = waitButtonRelease();
    if (source == WAKEUP_BOTH_BUTTONS) return CONFIG_MODE;
    if (source == WAKEUP_USER_BUTTON) {
        return held_ms > BUTTON_LONG_PRESS_MS ? PAIRED_MODE : WORK_MODE;
    }
    if (source == WAKEUP_DEV_BUTTON) {
        return held_ms > BUTTON_LONG_PRESS_MS ? FACTORY_RESET_MODE : TEST_MODE;
    }
    return UNPAIRED_MODE;
}

static void handleButtonMode(SysMode mode)
{
    switch (mode) {
    case FACTORY_RESET_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: FACTORY_RESET");
        for (int i = 0; i < 3; ++i) Led.blink(LED_PERIOD_3);
        Matcher.clear_master_mac();
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRING");
        Led.blink(LED_PERIOD_1);
        if (Matcher.pair()) {
            for (int i = 0; i < 3; ++i) Led.blink(LED_PERIOD_1);
        } else {
            ESP_LOGE(MAIN_TAG, "Pairing failed or a master is already saved");
        }
        break;

    case WORK_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: WORK (outside button)");
        runWorkWithLed(false, 0);
        return;  // runWork already restored BLE standby

    case TEST_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: TEST");
        Led.blink(LED_PERIOD_2);
        break;

    case CONFIG_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: CONFIG");
        Led.blink(LED_PERIOD_2);
        break;

    case UNPAIRED_MODE:
        break;
    }

    State.resumeStandby();
}

void setup()
{
    Serial.begin(115200);
    Led.led_init();
    Power.power_init();
    // Keep the radar rail off before the first work session without changing
    // the existing radar driver.
    pinMode(RADAR_POWER_PIN, OUTPUT);
    digitalWrite(RADAR_POWER_PIN, RADAR_POWER_OFF);
    if (!Power.wakeup_gpio_init()) {
        ESP_LOGE(MAIN_TAG, "Button event initialization failed");
        return;
    }

    const uint8_t battery = Power.get_battery_value();
    if (battery == 0) ESP_LOGE(MAIN_TAG, "Battery is empty");

    if (!Matcher.has_master_mac()) {
        ESP_LOGI(MAIN_TAG, "No paired inside unit; buttons remain available");
        for (int i = 0; i < 2; ++i) Led.blink(LED_PERIOD_2);
    }

    if (!State.begin()) {
        ESP_LOGE(MAIN_TAG, "Unable to enter BLE Light-sleep standby");
    }
}

void loop()
{
    WakeupSource button_source = WAKEUP_POWER_ON;
    BleWakeEvent wake_event{};
    const OutsideStandbyEvent event =
        State.waitForEvent(&button_source, &wake_event);

    if (event == OutsideStandbyEvent::BleWake) {
        uint8_t paired_master[6]{};
        if (Matcher.load_master_mac(paired_master) &&
            memcmp(paired_master, wake_event.master_mac, 6) == 0) {
            runWorkWithLed(true, wake_event.session_id);
        } else {
            ESP_LOGW(MAIN_TAG,
                     "BLE wake ignored: advertised master does not match pairing");
        }
        return;
    }

    if (event == OutsideStandbyEvent::Button) {
        State.pauseStandby();
        handleButtonMode(detectButtonMode(button_source));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
