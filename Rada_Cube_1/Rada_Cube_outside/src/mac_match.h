#ifndef __MAC_MATCH_H__
#define __MAC_MATCH_H__

#include <Preferences.h>
#include "espnow.h"
#include "protocol.h"

// ======================== Pairing parameters ========================
#define PAIR_MAX_RETRY          60      // Maximum number of pairing retries
#define PAIR_POLL_INTERVAL_MS   100     // Pairing polling interval (ms)
#define PAIR_ROUND_CHECKS       10      // Number of checks after each round of sending (PAIR_ROUND_CHECKS * PAIR_POLL_INTERVAL_MS = 1 round of total waiting time)

/**
 * MAC address pairing module
 *
 * Responsibility: Complete master-slave pairing through ESP-NOW and persist the other party's MAC address to NVS
 *
 * Usage example:
 *
 *   EspNowManager Espnow;
 *   MacMatch matcher(Espnow);
 *
 *   // Check if paired
 *   if (matcher.hasPeerMac()) {
 *       uint8_t mac[6];
 *       matcher.loadPeerMac(mac);
 *   }
 *
 *   // Perform pairing (blocking, waiting at most PAIR_MAX_RETRY rounds)
 *   if (!matcher.pair()) {
 *       // Pairing failure processing
 *   }
 *
 *   // Clear saved pairing information
 *   matcher.clearPeerMac();
 */
class MacMatch
{
public:
    // Pass in the EspNowManager reference during construction
    MacMatch(EspNowManager& espnow) : _espnow(espnow) {}

    // Execute the pairing process (blocking, returning false after timeout)
    bool pair(uint8_t max_retry = PAIR_MAX_RETRY);

    // NVS operation
    // Host NVS operation
// #ifdef INSIDE
//     bool has_slave_a_mac();
//     bool has_slave_b_mac();
//     bool load_slave_mac(uint8_t mac_out[6], uint8_t slave_id);
//     void save_slave_mac(const uint8_t mac[6], uint8_t slave_id);
//     void clear_slave_mac();

// #endif
    // Slave NVS operation
// #ifdef OUTSIDE
    bool has_master_mac();
    bool load_master_mac(uint8_t mac_out[6]);
    void save_master_mac(const uint8_t mac[6]);
    void clear_master_mac();

// #endif

private:
    EspNowManager& _espnow;
    Preferences    _prefs;
// #ifdef INSIDE
//     uint8_t        _slave_a_mac[6]{};
//     uint8_t        _slave_b_mac[6]{};
// #endif

// #ifdef OUTSIDE
    uint8_t        _peer_mac[6]{};
// #endif
};

#endif
