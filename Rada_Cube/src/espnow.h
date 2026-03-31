#ifndef __ESPNOW_H__
#define __ESPNOW_H__

#include <esp_now.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ======================== 常量 ========================
extern const uint8_t ESPNOW_BROADCAST[6];

// 接收消息最大负载
#define ESPNOW_MAX_DATA     32

// ======================== 接收消息结构 ========================
typedef struct {
    uint8_t  src_mac[6];            // 发送方 MAC
    uint8_t  data[ESPNOW_MAX_DATA]; // 负载
    uint8_t  len;                   // 实际负载长度
} espnow_msg_t;

/**
 * ESP-NOW 通信管理（纯通信层，不含业务逻辑）
 *
 * 接收原理：
 *   回调 → 把最新一帧写入 buffer + 置标志位
 *   主循环 → 检查标志位 → 读走数据 → 清标志位
 *   只保留最新一帧，旧的自动被覆盖（对雷达场景刚好合适）
 *
 * 使用示例：
 *
 *   EspNowManager Espnow;
 *
 *   // 1. 初始化（幂等，重复调用自动跳过）
 *   Espnow.init();
 *
 *   // 2. 添加对方地址 + 发送
 *   Espnow.addPeer(peer_mac);
 *   Espnow.send(peer_mac, data, len);
 *
 *   // 3. 接收
 *   Espnow.recvStart();
 *   espnow_msg_t msg;
 *   if (Espnow.read(&msg)) {
 *       // 处理 msg.data, msg.len, msg.src_mac
 *   }
 *   Espnow.recvStop();
 *
 *   // 4. 反初始化
 *   Espnow.deinit();
 */
class EspNowManager
{
public:
    // 初始化 WiFi STA + ESP-NOW（幂等，重复调用自动跳过）
    void init();

    // 反初始化 ESP-NOW
    void deinit();

    // 获取本机 MAC
    void getMac(uint8_t mac_out[6]);

    // 添加 / 删除 peer
    bool addPeer(const uint8_t mac[6]);
    bool delPeer(const uint8_t mac[6]);

    // 发送数据
    esp_err_t send(const uint8_t* peer_mac, const uint8_t* data, size_t len);

    // 开启接收（注册回调）
    void recvStart();

    // 是否有新数据到达
    bool hasNewData();

    // 读走最新一帧，读完后自动清除标志位
    bool read(espnow_msg_t* msg_out);

    // 停止接收（注销回调）
    void recvStop();

private:
    bool _inited = false;

    // ---- 接收缓冲（只保留最新一帧） ----
    espnow_msg_t     _rx_buf{};
    volatile bool    _rx_new_data = false;

    // ---- 串口回调转发 ----
    static EspNowManager* _instance;
    static void _onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    void _onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len);
};

#endif
