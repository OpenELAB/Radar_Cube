#ifndef OUTSIDE_STATE_CONTROLLER_H
#define OUTSIDE_STATE_CONTROLLER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "ble_wake_scanner.h"
#include "espnow.h"
#include "mac_match.h"
#include "radar.h"
#include "sensor.h"

enum class OutsideStandbyEvent {
    BleWake,
    Button,
    Error,
};

class OutsideStateController {
public:
    OutsideStateController(BleWakeScanner& scanner,
                           PowerManager& power,
                           EspNowManager& espnow,
                           MacMatch& matcher,
                           RadarModule& radar);

    bool begin();
    OutsideStandbyEvent waitForEvent(WakeupSource* button_source,
                                     BleWakeEvent* wake_event = nullptr);
    void pauseStandby();
    bool resumeStandby();
    void runWork(bool started_by_ble, uint32_t wake_session = 0);

private:
    void sendControlFrame(const uint8_t* peer_mac, frame_type_t type,
                          int repeat_count, uint32_t session_id = 0);
    void sendStandbyAckGrace(const uint8_t* peer_mac, uint32_t session_id);

    BleWakeScanner& _scanner;
    PowerManager& _power;
    EspNowManager& _espnow;
    MacMatch& _matcher;
    RadarModule& _radar;
    QueueSetHandle_t _event_set = nullptr;
    bool _standby = false;
};

#endif
