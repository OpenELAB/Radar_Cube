#include <Arduino.h>
#include <HardwareSerial.h>
#include "lora.h"
#include "protocol.h"

// NVS 键名
static const char* NVS_NAMESPACE = "lora";
static const char* NVS_KEY_FLAG  = "configured";

// Lora 模块初始化 AT 命令序列
static const char* LORA_INIT_CMDS[] = {
    "AT+MODE=0",
    "AT+RFCH=18",
    "AT+PID=255",
    "AT+MAMP=2",
    "AT+MLPWR=2",
    "AT+MID=17",
    "AT+MODE=1"
};
static const int LORA_INIT_CMD_COUNT = sizeof(LORA_INIT_CMDS) / sizeof(LORA_INIT_CMDS[0]);

#ifdef INSIDE
// 车内模块 Lora 睡眠命令
static const char* LORA_SLEEP_CMDS[] = {
    "AT+MODE=0",
    "AT+MAMP=0",
    "AT+LPWR=1",
    "AT+MODE=1"
};
static const int LORA_SLEEP_CMD_COUNT = sizeof(LORA_SLEEP_CMDS) / sizeof(LORA_SLEEP_CMDS[0]);

// 车内模块 Lora 唤醒命令
static const char* LORA_WAKEUP_CMDS[] = {
    "AT+MODE=0",
    "AT+MAMP=2",
    "AT+LPWR=0",
    "AT+MODE=1"
};
static const int LORA_WAKEUP_CMD_COUNT = sizeof(LORA_WAKEUP_CMDS) / sizeof(LORA_WAKEUP_CMDS[0]);
#endif

// ======================== NVS 操作 ========================

bool LoraManager::isConfigured()
{
    _prefs.begin(NVS_NAMESPACE, true);          // 只读
    bool flag = _prefs.getBool(NVS_KEY_FLAG, false);
    _prefs.end();
    return flag;
}

void LoraManager::setConfigured(bool flag)
{
    _prefs.begin(NVS_NAMESPACE, false);         // 读写
    _prefs.putBool(NVS_KEY_FLAG, flag);
    _prefs.end();
}

void LoraManager::clearConfigFlag()
{
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.remove(NVS_KEY_FLAG);
    _prefs.end();
}

// ======================== 硬件初始化 ========================

void LoraManager::init()
{
    LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);

#ifdef INSIDE
    // 车内模块需要手动开 Lora 电源，车外模块 Lora 是常开的
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);
#endif
}

// ======================== AT 指令 ========================

bool LoraManager::sendAT(const char* cmd, int timeout_ms, uint8_t max_retry)
{
    if (!cmd) {
        ESP_LOGE(LORA_TAG, "AT cmd is NULL");
        return false;
    }

    for (uint8_t retry = 0; retry < max_retry; retry++)
    {
        // 清空接收缓冲区
        while (LoraSerial.available()) LoraSerial.read();

        LoraSerial.println(cmd);

        // 等待响应
        uint32_t t_start = millis();
        String response = "";
        while (millis() - t_start < (uint32_t)timeout_ms)
        {
            if (LoraSerial.available())
            {
                char c = LoraSerial.read();
                response += c;
                if (c == '\n')
                {
                    response.trim();
                    if (response == "OK") {
                        ESP_LOGI(LORA_TAG, "[OK] %s", cmd);
                        return true;
                    }
                    if (response == "ERROR") {
                        ESP_LOGE(LORA_TAG, "[ERROR] %s", cmd);
                        break;      // 跳出内层 while, 进入重试
                    }
                    response = "";  // 清空，继续读下一行
                }
            }
        }
        ESP_LOGE(LORA_TAG, "[Timeout/Retry %d] %s", retry + 1, cmd);
    }
    return false;
}

// ======================== 首次配置 ========================

bool LoraManager::configure()
{
    if (isConfigured()) {
        ESP_LOGI(LORA_TAG, "Lora already configured, skip");
        return true;
    }

    ESP_LOGI(LORA_TAG, "First time config...");

    // CE 拉低进入配置模式
    digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);

    for (int i = 0; i < LORA_INIT_CMD_COUNT; i++)
    {
        if (!sendAT(LORA_INIT_CMDS[i])) {
            ESP_LOGE(LORA_TAG, "Config failed at: %s", LORA_INIT_CMDS[i]);
            digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
            return false;
        }
    }

    // CE 拉高回到正常工作模式
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
    // 记录已配置
    setConfigured(true);
    ESP_LOGI(LORA_TAG, "Config done");
    return true;
}

// ======================== 组合接口 ========================

void LoraManager::setup()
{
    init();
    configure();
}

// ======================== 发送唤醒帧 ========================

void LoraManager::sendWakeFrame()
{
    protocol_frame_t frame;
    frame_build(&frame, MASTER_FRAME_HEAD, FRAME_WAKE);

    digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);
    vTaskDelay(pdMS_TO_TICKS(10));  // TODO: 确保进入配置模式,记得是2ms,这里加10ms以防万一？
    LoraSerial.write((uint8_t*)&frame, sizeof(frame));
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
}

// ======================== 关闭 ========================

void LoraManager::shutdown()
{
    digitalWrite(LORA_POWER_PIN, LORA_POWER_OFF);
    LoraSerial.end();
}

