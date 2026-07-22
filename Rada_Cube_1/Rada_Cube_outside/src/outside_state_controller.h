#ifndef OUTSIDE_STATE_CONTROLLER_H
#define OUTSIDE_STATE_CONTROLLER_H

#include "espnow.h"
#include "mac_match.h"
#include "radar.h"
#include "sensor.h"

class OutsideStateController {
public:
    OutsideStateController(PowerManager& power,
                           EspNowManager& espnow,
                           MacMatch& matcher,
                           RadarModule& radar);

    void runWork(bool started_by_ble, uint32_t wake_session = 0);
    void shutdownWireless();

private:
    void sendControlFrame(const uint8_t* peer_mac, frame_type_t type,
                          int repeat_count, uint32_t session_id = 0);
    void sendStandbyAckGrace(const uint8_t* peer_mac, uint32_t session_id);

    PowerManager& _power;
    EspNowManager& _espnow;
    MacMatch& _matcher;
    RadarModule& _radar;
};

#endif
