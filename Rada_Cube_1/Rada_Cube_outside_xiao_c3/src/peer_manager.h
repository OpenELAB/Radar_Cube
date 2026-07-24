#ifndef XIAO_C3_PEER_MANAGER_H
#define XIAO_C3_PEER_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

#include "espnow.h"

class PeerManager {
public:
    explicit PeerManager(EspNowManager& espnow) : _espnow(espnow) {}

    bool hasMaster();
    bool loadMaster(uint8_t master_mac[6]);
    bool pair(uint32_t timeout_ms);
    void clear();

private:
    bool saveMaster(const uint8_t master_mac[6]);
    void stopWireless();

    EspNowManager& _espnow;
    Preferences _preferences;
};

#endif
