#ifndef __MASTER_ESPNOW_H__
#define __MASTER_ESPNOW_H__

#include <Arduino.h>
#include <Preferences.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "../pinconfig.h"




class espnow_master
{
public:
    void wifi_init();
    bool read_slave_mac();
    void broadcast_mode_init();
    static void broadcast_mode_send_result(const uint8_t* mac_addr, esp_now_send_status_t status);
    static void broadcast_mode_recv(const esp_now_recv_info_t* recv_info, const uint8_t* recv_data, int len);
    void broadcast_mode_loop();
    void delete_slave_mac();
    void unicast_mac_add(uint8_t* mac_addr);
    void unicast_mode_init();
    static void unicast_mode_recv(const esp_now_recv_info_t* recv_info, const uint8_t* recv_data, int len);
private:
    static Preferences prefs;
    uint8_t _master_mac[6]{};   // Master MAC address
    uint8_t left_slave_mac[6]{};   // Left slave MAC address
    uint8_t right_slave_mac[6]{};   // Right slave MAC address
    static constexpr uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   // Broadcast MAC address
    esp_now_peer_info_t _peer_info{}; 
};


#endif
