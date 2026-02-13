#ifndef __MAC_MATCH_H__
#define __MAC_MATCH_H__

#include <Preferences.h>
#include <esp_now.h>
#include "pins.h"
#include "protocol.h"



// 单例模式

class MacMatch
{
public:
    // 获取单例实例
    static MacMatch& getInstance()
    {
        static MacMatch instance;
        return instance;
    }

    // 删除拷贝构造函数和赋值
    MacMatch(const MacMatch&) = delete;
    MacMatch& operator=(const MacMatch&) = delete;

    // 初始化esp-now
    void mac_init();
    // 获取mac地址
    void get_mac_addr();

    // 对NVS空间的处理
    // MAC地址存在标志位KEY
    bool key_exist_flag();
    bool get_mac_exist_flag();
    void write_mac_exist_flag(bool flag);
    bool clear_mac_exist_flag();
    // MAC地址
    void write_mac_addr(const uint8_t* mac_addr);
    bool read_mac_addr(uint8_t* mac_buf, size_t buf_len);
    void clear_mac_addr();

    // 车内模块广播模式
    void broadcast_init();
    void broadcast_send();

    // 判断是否存在
    bool mac_exist();

    // 总处理逻辑
    bool mac_match();

private:

    // 构造函数私有化
    MacMatch() = default;
    ~MacMatch() = default;

    Preferences mac_prefe;
    uint8_t mac[6]{};       // 用于存储NVS里读取的mac地址
    uint8_t _self_mac[6]{}; // 本机mac地址

#ifdef INSIDE
    uint8_t _slave_mac[6]{};
#endif

#ifdef OUTSIDE
    uint8_t _master_mac[6]{};
#endif
    // 记录从机地址的标志位
    bool slave_mac_flag = false;
    // 记录主机地址的标志位
    bool master_mac_flag = false;

    // ======================== 静态回调函数 ========================
    static void broadcast_onsent(const esp_now_send_info_t* info, esp_now_send_status_t status);
    static void broadcast_onrece(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    // 车外模块设置为接收模式，接收处理数据帧并保存主机地址到NVS里
    static void slave_onsent(const esp_now_send_info_t* info, esp_now_send_status_t status);
    static void slave_onrece(const esp_now_recv_info_t* info, const uint8_t* data, int len);

    // ======================== 实例方法 ========================
    void onBroadcastSent(const esp_now_send_info_t* info, esp_now_send_status_t status);
    void onBroadcastRece(const esp_now_recv_info_t* info, const uint8_t* data, int len);


    void onSlaveSent(const esp_now_send_info_t* info, esp_now_send_status_t status);
    void onSlaveRece(const esp_now_recv_info_t* info, const uint8_t* data, int len);
};











#endif
