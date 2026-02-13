#ifndef __LORA_H__
#define __LORA_H__

#include <Preferences.h>
#include "pins.h"
#include "config.h"

/**
 * Lora 无线模块管理
 *
 * 使用示例：
 *
 *   LoraManager Lora;
 *
 *   // 初始化 + AT 配置（已配过则自动跳过）
 *   Lora.setup();
 *
 *   // 发送唤醒帧
 *   Lora.sendWakeFrame();
 *
 *   // 关闭模块
 *   Lora.shutdown();
 */
class LoraManager
{
public:
    // 初始化串口 + CE引脚 + 电源
    void init();

    // 首次配置 AT 参数（已配置过则跳过），返回是否成功
    bool configure();

    // init() + configure() 的组合，给 main 调用
    void setup();

    // 发送 AT 指令并等待 OK / ERROR，支持重试
    bool sendAT(const char* cmd, int timeout_ms = LORA_AT_TIMEOUT, uint8_t max_retry = LORA_AT_RETRY);

    // 通过 Lora 发送无线唤醒帧
    void sendWakeFrame();

    // 关闭 Lora 模块（拉低电源 + 关串口）
    void shutdown();

    // 清除 NVS 中的已配置标志（调试/重置用）
    void clearConfigFlag();

private:
    Preferences _prefs;

    // NVS 相关
    bool isConfigured();
    void setConfigured(bool flag);
};

#endif
