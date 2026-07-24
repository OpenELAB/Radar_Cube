#include <Arduino.h>
#include <cstring>

#include <esp_log.h>

#include "app_config.h"
#include "ble_standby_scanner.h"
#include "espnow.h"
#include "low_power_policy.h"
#include "peer_manager.h"
#include "wake_session_controller.h"

namespace {
EspNowManager espnow;
PeerManager peers(espnow);
BleStandbyScanner scanner;
WakeSessionController session(espnow);

uint8_t master_mac[6]{};
bool standby_ready = false;

bool ensurePaired()
{
    if (peers.loadMaster(master_mac)) {
        if constexpr (POWER_TEST_LOG_ENABLED) {
            ESP_LOGI("XIAO_MAIN",
                     "Loaded inside MAC %02X:%02X:%02X:%02X:%02X:%02X",
                     master_mac[0], master_mac[1], master_mac[2],
                     master_mac[3], master_mac[4], master_mac[5]);
        }
        return true;
    }

    if constexpr (POWER_TEST_LOG_ENABLED) {
        ESP_LOGI("XIAO_MAIN",
                 "No inside MAC saved; waiting %lu ms for ESP-NOW pairing",
                 static_cast<unsigned long>(AUTO_PAIR_TIMEOUT_MS));
    }
    if (!peers.pair(AUTO_PAIR_TIMEOUT_MS) ||
        !peers.loadMaster(master_mac)) {
        if constexpr (POWER_TEST_LOG_ENABLED) {
            ESP_LOGE("XIAO_MAIN", "Pairing timed out");
        }
        return false;
    }

    if constexpr (POWER_TEST_LOG_ENABLED) {
        ESP_LOGI("XIAO_MAIN",
                 "Paired inside MAC %02X:%02X:%02X:%02X:%02X:%02X",
                 master_mac[0], master_mac[1], master_mac[2],
                 master_mac[3], master_mac[4], master_mac[5]);
    }
    return true;
}

bool startStandby()
{
    LowPowerPolicy::enterBleStandby();
    if (!scanner.begin(master_mac)) {
        if constexpr (POWER_TEST_LOG_ENABLED) {
            ESP_LOGE("XIAO_MAIN", "BLE scanner initialization failed");
        }
        return false;
    }
    if (!scanner.start(STANDBY_SCAN_INTERVAL_MS,
                       STANDBY_SCAN_WINDOW_MS)) {
        if constexpr (POWER_TEST_LOG_ENABLED) {
            ESP_LOGE("XIAO_MAIN", "BLE scan start failed");
        }
        scanner.pauseScan();
        return false;
    }

    if constexpr (POWER_TEST_LOG_ENABLED) {
        ESP_LOGI("XIAO_MAIN",
                 "Standby scan started: interval=%lu ms, window=%lu ms",
                 static_cast<unsigned long>(STANDBY_SCAN_INTERVAL_MS),
                 static_cast<unsigned long>(STANDBY_SCAN_WINDOW_MS));
    }
    return true;
}
}

void setup()
{
#if POWER_TEST_LOG_ENABLED
    Serial.begin(115200);
    delay(100);
    esp_log_level_set("XIAO_MAIN", ESP_LOG_INFO);
    esp_log_level_set("BLE_STANDBY", ESP_LOG_INFO);
    esp_log_level_set("XIAO_SESSION", ESP_LOG_INFO);
    esp_log_level_set("ESPNOW", ESP_LOG_INFO);
#endif

    LowPowerPolicy::begin(POWER_TEST_LOG_ENABLED != 0);
    LowPowerPolicy::enterBleStandby();

    while (!ensurePaired()) {
        LowPowerPolicy::idle(pdMS_TO_TICKS(2000));
    }

    standby_ready = startStandby();
}

void loop()
{
    if (!standby_ready) {
        LowPowerPolicy::idle(pdMS_TO_TICKS(1000));
        standby_ready = startStandby();
        return;
    }

    BleWakeEvent wake_event{};
    if (!scanner.waitForWake(&wake_event, portMAX_DELAY)) {
        if constexpr (POWER_TEST_LOG_ENABLED) {
            ESP_LOGE("XIAO_MAIN", "BLE standby wait ended unexpectedly");
        }
        scanner.pauseScan();
        standby_ready = false;
        return;
    }

    if constexpr (POWER_TEST_LOG_ENABLED) {
        ESP_LOGI("XIAO_MAIN",
                 "Wake event dequeued, session=%08lX, pausing BLE scan",
                 static_cast<unsigned long>(wake_event.session_id));
    }

    // Keep the resident NimBLE host/controller initialized and stop only the
    // scan procedure. Observer-only NimBLE builds do not initialize connection
    // queues, and this ESP-IDF version asserts while deinitializing those
    // absent queues. A paused BLE controller can coexist with ESP-NOW and is
    // resumed by startStandby() after the session.
    scanner.pauseScan();
    standby_ready = false;

    if constexpr (POWER_TEST_LOG_ENABLED) {
        ESP_LOGI("XIAO_MAIN",
                 "BLE scan paused, starting ESP-NOW session");
    }

    const WakeSessionResult result = session.run(wake_event, master_mac);
    if constexpr (POWER_TEST_LOG_ENABLED) {
        ESP_LOGI("XIAO_MAIN", "Wake session finished with result=%u",
                 static_cast<unsigned>(result));
    }

    standby_ready = startStandby();
}
