#ifndef BLE_WAKE_SCANNER_H
#define BLE_WAKE_SCANNER_H

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

struct BleWakeEvent {
    uint8_t master_mac[6];
    uint32_t session_id;
    int rssi;
};

class BleWakeScanner : private BLEAdvertisedDeviceCallbacks {
public:
    bool begin();
    bool start();
    void stop();
    QueueHandle_t eventQueue() const { return _event_queue; }
    void clearEvents();

private:
    void onResult(BLEAdvertisedDevice device) override;

    QueueHandle_t _event_queue = nullptr;
    bool _ble_initialized = false;
    bool _scanning = false;
    uint8_t _last_master_mac[6]{};
    uint32_t _last_session_id = 0;
};

#endif
