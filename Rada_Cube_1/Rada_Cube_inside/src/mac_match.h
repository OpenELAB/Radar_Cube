#ifndef __MAC_MATCH_H__
#define __MAC_MATCH_H__

#include <Preferences.h>
#include "espnow.h"
#include "protocol.h"

// ======================== 配对参数 ========================
#define PAIR_MAX_RETRY          60      // 配对最大重试次数
#define PAIR_POLL_INTERVAL_MS   100     // 配对轮询间隔 (ms)
#define PAIR_ROUND_CHECKS       10      // 每轮发送后检查次数（PAIR_ROUND_CHECKS * PAIR_POLL_INTERVAL_MS = 1轮总等待时间）

/**
 * MAC 地址配对模块
 *
 * 责任：通过 ESP-NOW 完成主从配对，并把对方 MAC 地址持久化到 NVS
 *
 * 使用示例：
 *
 *   EspNowManager Espnow;
 *   MacMatch matcher(Espnow);
 *
 *   // 检查是否已配对
 *   if (matcher.hasPeerMac()) {
 *       uint8_t mac[6];
 *       matcher.loadPeerMac(mac);
 *   }
 *
 *   // 执行配对（阻塞，最多等 PAIR_MAX_RETRY 轮）
 *   if (!matcher.pair()) {
 *       // 配对失败处理
 *   }
 *
 *   // 清除已保存的配对信息
 *   matcher.clearPeerMac();
 */
class MacMatch
{
public:
    typedef void (*PairEventCallback)(uint8_t slave_id, const uint8_t mac[6], void* context);

    // 构造时传入 EspNowManager 引用
    MacMatch(EspNowManager& espnow) : _espnow(espnow) {}

    // 执行配对流程（阻塞，超时返回 false）
    bool pair(uint8_t max_retry = PAIR_MAX_RETRY,
              PairEventCallback callback = nullptr,
              void* callback_context = nullptr);

    // NVS 操作
    // 主机NVS操作
    bool has_slave_a_mac();
    bool has_slave_b_mac();
    bool load_slave_mac(uint8_t mac_out[6], uint8_t slave_id);
    void save_slave_mac(const uint8_t mac[6], uint8_t slave_id);
    void clear_slave_mac();
    // 从机NVS操作

private:
    EspNowManager& _espnow;
    Preferences    _prefs;
    uint8_t        _slave_a_mac[6]{};
    uint8_t        _slave_b_mac[6]{};
};

#endif
