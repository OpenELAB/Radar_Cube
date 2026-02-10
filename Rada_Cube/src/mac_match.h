#ifndef __MAC_MATCH_H__
#define __MAC_MATCH_H__

#include <Preferences.h>
#include <esp_now.h>





class MacMatch
{
public:
    // 初始化esp-now
    void mac_match_init();

    // 对NVS空间的处理
    bool key_exist_flag();
    bool get_mac_exist_flag();
    void write_mac_exist_flag(bool flag);
    bool clear_mac_exist_flag();
    void write_mac_addr(char* mac_addr);

    // 车内模块广播模式
    void broadcast_init();
    void broadcast_send();
    static void broadcast_onsent(const esp_now_send_info_t* info, esp_now_send_status_t status);
    static void broadcast_onrece(const esp_now_recv_info_t* info, const uint8_t* data, int len);


    // 总处理逻辑
    void mac_match();

private:
    Preferences mac_prefe;
    uint8_t mac[6]{};       // 用于存储NVS里读取的mac地址
    uint8_t _self_mac[6]{}; // 本机mac地址

    // 记录从机地址的标志位
    bool slave1_mac_flag = false;
};











#endif
