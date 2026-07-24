#ifndef BLE_STANDBY_SCANNER_H
#define BLE_STANDBY_SCANNER_H

#include <Arduino.h>

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>

struct ble_gap_event;

struct BleWakeEvent {
    uint8_t master_mac[6];
    uint32_t session_id;
    int rssi;
};

// A resident passive scanner intended for automatic light sleep.
//
// NimBLE is initialized once by start() and remains initialized while scans are
// paused. Only shutdown() stops and deinitializes the NimBLE host/controller.
// The application task should normally block in waitForWake(), allowing the
// idle task and ESP-IDF power manager to enter light sleep between scan windows.
class BleStandbyScanner {
public:
    bool begin(const uint8_t expected_master[6]);
    bool start(uint32_t interval_ms, uint32_t window_ms);
    bool waitForWake(BleWakeEvent* event,
                     TickType_t timeout_ticks = portMAX_DELAY);

    void pauseScan();
    bool resumeScan();
    void shutdown();

    bool isInitialized() const { return _ble_initialized; }
    bool isScanning() const { return _scanning.load(); }
    QueueHandle_t eventQueue() const { return _event_queue; }
    void clearEvents();

private:
    static void hostTask(void* parameter);
    static void onHostReset(int reason);
    static void onHostSync();
    static int onGapEvent(ble_gap_event* event, void* argument);

    bool initializeBle();
    bool startConfiguredScan();
    void onAdvertisement(const uint8_t* manufacturer_data,
                         size_t manufacturer_data_length, int rssi);

    QueueHandle_t _event_queue = nullptr;
    EventGroupHandle_t _host_events = nullptr;

    bool _configured = false;
    bool _ble_initialized = false;
    std::atomic<bool> _scanning{false};
    std::atomic<bool> _accept_events{false};

    uint8_t _own_address_type = 0;
    uint16_t _scan_interval_units = 0;
    uint16_t _scan_window_units = 0;
    uint8_t _expected_master_mac[6]{};

    // Accessed only from the NimBLE host callback.
    bool _has_last_session = false;
    uint8_t _last_master_mac[6]{};
    uint32_t _last_session_id = 0;
};

#endif
