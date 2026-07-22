#include <Arduino.h>
#include <cstring>

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

namespace {
constexpr uint32_t RTC_PAIR_MAGIC = 0x52435043;  // "RCPC"
constexpr uint16_t RTC_PAIR_VERSION = 1;

struct RtcPairCache {
    uint32_t magic;
    uint16_t version;
    uint8_t paired;
    uint8_t master_mac[6];
    uint32_t checksum;
    uint32_t standby_wake_count;
    uint32_t prng_state;
};

RTC_DATA_ATTR RtcPairCache rtc_pair_cache{};
}

HardwareSerial& RadarSerial = Serial1;
RadarModule Radar;
LEDControler Led;
PowerManager Power;
BleWakeScanner BleScanner;
EspNowManager Espnow;
MacMatch Matcher(Espnow);
OutsideStateController State(Power, Espnow, Matcher, Radar);

static void configureAppLogging()
{
    // Keep framework startup quiet on every RTC wake, then re-enable only the
    // application diagnostics needed to verify the fast standby path.
    esp_log_level_set(MAIN_TAG, ESP_LOG_INFO);
    esp_log_level_set(POWER_TAG, ESP_LOG_INFO);
    esp_log_level_set(BLE_WAKE_TAG, ESP_LOG_INFO);
    esp_log_level_set(MAC_TAG, ESP_LOG_INFO);
    esp_log_level_set(RADAR_TAG, ESP_LOG_INFO);
    esp_log_level_set("ESPNOW", ESP_LOG_INFO);
}

static uint32_t pairCacheChecksum(const RtcPairCache& cache)
{
    uint32_t value = RTC_PAIR_MAGIC ^ RTC_PAIR_VERSION ^ cache.paired;
    for (uint8_t byte : cache.master_mac) {
        value = (value * 16777619UL) ^ byte;
    }
    return value;
}

static bool pairCacheValid()
{
    return rtc_pair_cache.magic == RTC_PAIR_MAGIC &&
           rtc_pair_cache.version == RTC_PAIR_VERSION &&
           rtc_pair_cache.checksum == pairCacheChecksum(rtc_pair_cache);
}

static void savePairCache(bool paired, const uint8_t* master_mac = nullptr)
{
    rtc_pair_cache.magic = RTC_PAIR_MAGIC;
    rtc_pair_cache.version = RTC_PAIR_VERSION;
    rtc_pair_cache.paired = paired ? 1 : 0;
    memset(rtc_pair_cache.master_mac, 0, sizeof(rtc_pair_cache.master_mac));
    if (paired && master_mac) {
        memcpy(rtc_pair_cache.master_mac, master_mac,
               sizeof(rtc_pair_cache.master_mac));
    }
    rtc_pair_cache.checksum = pairCacheChecksum(rtc_pair_cache);
}

static bool refreshPairCacheFromNvs()
{
    uint8_t master_mac[6]{};
    const bool paired = Matcher.has_master_mac() &&
                        Matcher.load_master_mac(master_mac);
    savePairCache(paired, paired ? master_mac : nullptr);
    return paired;
}

static bool loadCachedMasterMac(uint8_t master_mac[6])
{
    if (!pairCacheValid() && !refreshPairCacheFromNvs()) return false;
    if (!rtc_pair_cache.paired) return false;
    memcpy(master_mac, rtc_pair_cache.master_mac, 6);
    return true;
}

static uint32_t nextStandbySleepMs()
{
    uint32_t state = rtc_pair_cache.prng_state;
    if (state == 0) {
        state = static_cast<uint32_t>(ESP.getEfuseMac()) ^ 0x9E3779B9UL;
        if (state == 0) state = 1;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    rtc_pair_cache.prng_state = state;
    return OUTSIDE_DEEP_SLEEP_MS +
           (state % (static_cast<uint32_t>(OUTSIDE_SLEEP_JITTER_MS) + 1U));
}

[[noreturn]] static void enterStandby()
{
    BleScanner.stop();
    State.shutdownWireless();

    const bool paired = pairCacheValid() && rtc_pair_cache.paired;
    if (paired) ++rtc_pair_cache.standby_wake_count;
    Power.enterDeepSleep(paired ? nextStandbySleepMs() : 0, paired);
}

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
    Power.clearButtonEvents();
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
        savePairCache(false);
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRING");
        Led.blink(LED_PERIOD_1);
        if (Matcher.pair()) {
            refreshPairCacheFromNvs();
            for (int i = 0; i < 3; ++i) Led.blink(LED_PERIOD_1);
        } else {
            ESP_LOGE(MAIN_TAG, "Pairing failed or a master is already saved");
            refreshPairCacheFromNvs();
        }
        break;

    case WORK_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: WORK (outside button)");
        runWorkWithLed(false, 0);
        break;

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
}

static void initializeInteractiveHardware()
{
    Serial.begin(115200);
    Led.led_init();
    Power.power_init();
}

static void handleColdBoot()
{
    initializeInteractiveHardware();
    const uint8_t battery = Power.get_battery_value();
    if (battery == 0) ESP_LOGE(MAIN_TAG, "Battery is empty");

    if (!refreshPairCacheFromNvs()) {
        ESP_LOGI(MAIN_TAG, "No paired inside unit; waiting for button pairing");
        for (int i = 0; i < 2; ++i) Led.blink(LED_PERIOD_2);
    }

    if (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
        digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED) {
        handleButtonMode(detectButtonMode(Power.classifyWakeButtons()));
    }
}

static void handleButtonWake(WakeupSource source)
{
    initializeInteractiveHardware();
    refreshPairCacheFromNvs();
    handleButtonMode(detectButtonMode(source));
}

static bool handleTimerWake()
{
    uint8_t master_mac[6]{};
    if (!loadCachedMasterMac(master_mac)) return false;

    BleWakeEvent wake_event{};
    if (!BleScanner.scanBurst(master_mac, BLE_SCAN_BURST_MS, &wake_event)) {
        return false;
    }
    if (Power.buttonEventPending() ||
        digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
        digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED) {
        return false;
    }

    // From this point the unit is doing real work, so normal diagnostics are OK.
    Serial.begin(115200);
    ESP_LOGI(MAIN_TAG, "BLE wake accepted, session=%08lX, RSSI=%d",
             static_cast<unsigned long>(wake_event.session_id), wake_event.rssi);
    runWorkWithLed(true, wake_event.session_id);
    return true;
}

void setup()
{
    configureAppLogging();
    Power.prepareSafePins();
    const WakeupSource wakeup = Power.getWakeupSource();
    Power.beginButtonMonitoring();

    switch (wakeup) {
    case WAKEUP_TIMER: {
        const bool work_started = handleTimerWake();
        if (!work_started &&
            (Power.buttonEventPending() ||
            digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
             digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED)) {
            handleButtonWake(Power.classifyWakeButtons());
        }
        break;
    }

    case WAKEUP_USER_BUTTON:
    case WAKEUP_DEV_BUTTON:
    case WAKEUP_BOTH_BUTTONS:
        handleButtonWake(wakeup);
        break;

    case WAKEUP_POWER_ON:
    case WAKEUP_UNKNOWN:
    default:
        handleColdBoot();
        break;
    }

    enterStandby();
}

void loop()
{
    // Every wake cycle is completed in setup() and ends in deep sleep.
}
