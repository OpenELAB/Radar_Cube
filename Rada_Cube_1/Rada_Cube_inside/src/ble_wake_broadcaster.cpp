#include "ble_wake_broadcaster.h"

#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <esp_system.h>

#include "ble_wake_protocol.h"
#include "config.h"

bool BleWakeBroadcaster::start(const uint8_t master_mac[6])
{
    if (_running) return true;
    if (!master_mac) {
        ESP_LOGE(BLE_WAKE_TAG, "Master MAC is required for wake advertising");
        return false;
    }

    if (!_ble_initialized) {
        if (!BLEDevice::init("RadarCube-Inside")) {
            ESP_LOGE(BLE_WAKE_TAG, "BLE initialization failed");
            return false;
        }
        _ble_initialized = true;
    }

    _session_id = esp_random();
    if (_session_id == 0) _session_id = 1;

    BleWakePayload payload{};
    bleWakeBuild(&payload, master_mac, _session_id);

    BLEAdvertisementData data;
    data.setFlags(ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    data.setManufacturerData(String(
        reinterpret_cast<const char*>(&payload), sizeof(payload)));

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->stop();
    advertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    advertising->setScanResponse(false);
    advertising->setMinInterval(BLE_WAKE_ADV_INTERVAL_MIN);
    advertising->setMaxInterval(BLE_WAKE_ADV_INTERVAL_MAX);
    advertising->setAdvertisementData(data);

    _running = advertising->start(0);
    ESP_LOGI(BLE_WAKE_TAG,
             "Wake advertising %s, master=%02X:%02X:%02X:%02X:%02X:%02X, session=%08lX",
             _running ? "started" : "failed",
             master_mac[0], master_mac[1], master_mac[2], master_mac[3],
             master_mac[4], master_mac[5],
             static_cast<unsigned long>(_session_id));
    return _running;
}

void BleWakeBroadcaster::stop()
{
    if (_running) BLEDevice::getAdvertising()->stop();
    _running = false;

    if (_ble_initialized) {
        BLEDevice::deinit(true);
        _ble_initialized = false;
    }
    ESP_LOGI(BLE_WAKE_TAG, "Wake advertising stopped");
}
