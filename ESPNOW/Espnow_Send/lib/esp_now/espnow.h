#ifndef __ESPNOW_H__
#define __ESPNOW_H__

#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>


class ESPNOW_Rada
{
public:
    ESPNOW_Rada();
    void Rada_rece_init();
    String readSavedMac();
    void unicast_mode();
    void send_selfmac();
    void send_rada_data(uint8_t* data, size_t len);
private:
    static Preferences prefs;
    static void _onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    static void _recv_form_buzzer(const esp_now_recv_info_t *info, const uint8_t *data, int data_len);
    uint8_t _buzzer_mac[6] = {0};
    static ESPNOW_Rada* _instance;
};




#endif
