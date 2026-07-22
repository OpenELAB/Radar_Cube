#ifndef BLE_WAKE_SCANNER_H
#define BLE_WAKE_SCANNER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

struct ble_gap_event;

struct BleWakeEvent {
    uint8_t master_mac[6];
    uint32_t session_id;
    int rssi;
};

class BleWakeScanner {
public:
    bool begin();
    bool start();
    void stop();
    bool scanBurst(const uint8_t expected_master[6], uint32_t timeout_ms,
                   BleWakeEvent* result);
    QueueHandle_t eventQueue() const { return _event_queue; }
    void clearEvents();

private:
    static void hostTask(void* parameter);
    static void onHostReset(int reason);
    static void onHostSync();
    static int onGapEvent(ble_gap_event* event, void* argument);
    void onAdvertisement(const uint8_t* manufacturer_data,
                         size_t manufacturer_data_length, int rssi);

    QueueHandle_t _event_queue = nullptr;
    volatile bool _host_synced = false;
    bool _ble_initialized = false;
    bool _scanning = false;
    bool _filter_master = false;
    uint8_t _own_address_type = 0;
    uint8_t _expected_master_mac[6]{};
    uint8_t _last_master_mac[6]{};
    uint32_t _last_session_id = 0;
};

#endif
