#ifndef __ESPNOW_H__
#define __ESPNOW_H__

#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>


class ESPNOW_Buzzer
{
public: 
    ESPNOW_Buzzer();
    void broadcast_init();
    void broadcast_sent();
    String readSavedmac();
    void broadcast_loop();
    void unicast_mode();
    void send_to_rada(uint8_t* data, size_t len);
    uint16_t getMedianDistance();

private:
    static constexpr uint8_t BROADCAST_ADDR[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t _selfmac[6]{};
    uint8_t _Rada_mac[6]{};
    esp_now_peer_info_t _peer{};
    static Preferences prefs;
    static ESPNOW_Buzzer *_instance;
    static void _onSent(const uint8_t *mac, esp_now_send_status_t status);
    static void _onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    static void _Recv_rada(const esp_now_recv_info_t *info, const uint8_t *data, int len);

    static constexpr uint8_t MEDIAN_WIN = 5;
    static uint16_t distBuf[MEDIAN_WIN];
    static uint8_t bufIdx;
    static uint16_t median_of(uint16_t*arr, uint8_t n);

};




#endif
