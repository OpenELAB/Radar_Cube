#ifndef BLE_WAKE_BROADCASTER_H
#define BLE_WAKE_BROADCASTER_H

#include <Arduino.h>

struct ble_gap_event;

class BleWakeBroadcaster {
public:
    bool start(const uint8_t master_mac[6]);
    void stop();
    bool isRunning() const { return _running; }
    uint32_t sessionId() const { return _session_id; }

private:
    static void hostTask(void* parameter);
    static void onHostReset(int reason);
    static void onHostSync();
    static int onGapEvent(ble_gap_event* event, void* argument);

    volatile bool _host_synced = false;
    volatile bool _running = false;
    bool _ble_initialized = false;
    uint8_t _own_address_type = 0;
    uint32_t _session_id = 0;
};

#endif
