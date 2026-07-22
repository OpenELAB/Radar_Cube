#include "ble_wake_scanner.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <cstring>

#include "ble_wake_protocol.h"
#include "config.h"

bool BleWakeScanner::begin()
{
    if (!_event_queue) _event_queue = xQueueCreate(1, sizeof(BleWakeEvent));
    return _event_queue != nullptr;
}

bool BleWakeScanner::start()
{
    if (_scanning) return true;
    if (!begin()) return false;

    if (!_ble_initialized) {
        if (!BLEDevice::init("RadarCube-Outside")) {
            ESP_LOGE(BLE_WAKE_TAG, "BLE initialization failed");
            return false;
        }
        _ble_initialized = true;
    }

    clearEvents();
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(false);
    scan->setInterval(BLE_SCAN_INTERVAL_MS);
    scan->setWindow(BLE_SCAN_WINDOW_MS);
    // One valid report is enough to validate the Radar Cube payload. Let the
    // controller filter repeated advertisements so they do not wake the host
    // task throughout standby.
    scan->setAdvertisedDeviceCallbacks(this, false, true);
    _scanning = scan->start(
        0, static_cast<void (*)(BLEScanResults)>(nullptr), false);
    ESP_LOGI(BLE_WAKE_TAG, "Passive scan %s", _scanning ? "started" : "failed");
    return _scanning;
}

void BleWakeScanner::stop()
{
    if (_scanning) BLEDevice::getScan()->stop();
    _scanning = false;

    if (_ble_initialized) {
        // The outside controller returns to scanning without rebooting. Keep the
        // controller memory reusable so BLEDevice::init() can succeed next time.
        BLEDevice::deinit(false);
        _ble_initialized = false;
    }
}

void BleWakeScanner::clearEvents()
{
    if (_event_queue) {
        BleWakeEvent discarded{};
        while (xQueueReceive(_event_queue, &discarded, 0) == pdTRUE) {}
    }
}

void BleWakeScanner::onResult(BLEAdvertisedDevice device)
{
    if (!device.haveManufacturerData() || !_event_queue) return;

    const String data = device.getManufacturerData();
    uint32_t session_id = 0;
    uint8_t master_mac[BLE_WAKE_MAC_LENGTH]{};
    if (!bleWakeValidate(reinterpret_cast<const uint8_t*>(data.c_str()),
                         data.length(), &session_id, master_mac)) return;
    if (session_id == _last_session_id &&
        memcmp(master_mac, _last_master_mac, BLE_WAKE_MAC_LENGTH) == 0) return;

    _last_session_id = session_id;
    memcpy(_last_master_mac, master_mac, sizeof(_last_master_mac));
    BleWakeEvent event{};
    memcpy(event.master_mac, master_mac, sizeof(event.master_mac));
    event.session_id = session_id;
    event.rssi = device.getRSSI();
    xQueueOverwrite(_event_queue, &event);
    ESP_LOGI(BLE_WAKE_TAG,
             "Wake advertisement received, master=%02X:%02X:%02X:%02X:%02X:%02X, session=%08lX, RSSI=%d",
             event.master_mac[0], event.master_mac[1], event.master_mac[2],
             event.master_mac[3], event.master_mac[4], event.master_mac[5],
             static_cast<unsigned long>(session_id), event.rssi);
}
