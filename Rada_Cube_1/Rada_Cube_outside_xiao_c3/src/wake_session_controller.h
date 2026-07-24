#ifndef XIAO_C3_WAKE_SESSION_CONTROLLER_H
#define XIAO_C3_WAKE_SESSION_CONTROLLER_H

#include <Arduino.h>

#include "ble_standby_scanner.h"
#include "espnow.h"
#include "protocol.h"

enum class WakeSessionResult : uint8_t {
    Completed,
    Cancelled,
    ConfirmTimeout,
    WirelessError,
};

class WakeSessionController {
public:
    explicit WakeSessionController(EspNowManager& espnow) : _espnow(espnow) {}

    WakeSessionResult run(const BleWakeEvent& wake_event,
                          const uint8_t master_mac[6]);

private:
    void sendSessionFrame(const uint8_t master_mac[6], frame_type_t type,
                          uint32_t session_id, uint8_t repeat_count = 1);
    void sendRadarSample(const uint8_t master_mac[6]);
    void acknowledgeStandby(const uint8_t master_mac[6], uint32_t session_id);
    bool isMatchingControl(const espnow_msg_t& message,
                           const uint8_t master_mac[6],
                           frame_type_t type,
                           uint32_t session_id) const;
    void stopWireless();

    EspNowManager& _espnow;
};

#endif
