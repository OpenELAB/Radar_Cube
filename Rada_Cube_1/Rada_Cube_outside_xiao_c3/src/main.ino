#include <Arduino.h>
#include <cstring>

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
    if (peers.loadMaster(master_mac)) return true;

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
        scanner.shutdown();
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
    delay(30);
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
        scanner.shutdown();
        standby_ready = false;
        return;
    }

    // BLE and ESP-NOW can coexist, but shutting the BLE stack down on a real
    // wake keeps the test deterministic. This cost is paid only on a user
    // wake, never once per scan interval.
    scanner.shutdown();
    standby_ready = false;

    const WakeSessionResult result = session.run(wake_event, master_mac);
    if constexpr (POWER_TEST_LOG_ENABLED) {
        ESP_LOGI("XIAO_MAIN", "Wake session finished with result=%u",
                 static_cast<unsigned>(result));
    }

    standby_ready = startStandby();
}
