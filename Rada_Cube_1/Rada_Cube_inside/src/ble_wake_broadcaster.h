#ifndef BLE_WAKE_BROADCASTER_H
#define BLE_WAKE_BROADCASTER_H

#include <Arduino.h>

class BleWakeBroadcaster {
public:
    bool start(const uint8_t master_mac[6]);
    void stop();
    bool isRunning() const { return _running; }
    uint32_t sessionId() const { return _session_id; }

private:
    bool _running = false;
    bool _ble_initialized = false;
    uint32_t _session_id = 0;
};

#endif
