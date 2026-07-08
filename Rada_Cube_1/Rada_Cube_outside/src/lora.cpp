#include <Arduino.h>
#include <HardwareSerial.h>
#include <cstring>
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
    "AT+MLPWR=0",
    "AT+MID=17",
    "AT+MODE=1"
};
static const int LORA_INIT_CMD_COUNT = sizeof(LORA_INIT_CMDS) / sizeof(LORA_INIT_CMDS[0]);

// #ifdef INSIDE
// // 车内模块 Lora 睡眠命令
// static const char* LORA_SLEEP_CMDS[] = {
//     "AT+MODE=0",
//     "AT+MAMP=0",
//     "AT+LPWR=1",
//     "AT+MODE=1"
// };
// static const int LORA_SLEEP_CMD_COUNT = sizeof(LORA_SLEEP_CMDS) / sizeof(LORA_SLEEP_CMDS[0]);

// // 车内模块 Lora 唤醒命令
// static const char* LORA_WAKEUP_CMDS[] = {
//     "AT+MODE=0",
//     "AT+MAMP=2",
//     "AT+LPWR=0",
//     "AT+MODE=1"
// };
// static const int LORA_WAKEUP_CMD_COUNT = sizeof(LORA_WAKEUP_CMDS) / sizeof(LORA_WAKEUP_CMDS[0]);
// #endif

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
    // TODO：这里还得把Lora模块的电源打开，不然Lora配置会失败，具体不知道什么原因
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);
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

void LoraManager::sendWakeFrame(const uint8_t master_mac[6])
{
    lora_wake_frame_t frame;
    lora_wake_frame_build(&frame, master_mac);

    // 问题: 这里切换电平会导致第一次发送失败和导致校验位数据错误
    // digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);
    // vTaskDelay(pdMS_TO_TICKS(100));  // TODO: 确保进入配置模式,记得是2ms,这里加10ms以防万一？
    LoraSerial.write((uint8_t*)&frame, sizeof(frame));
    // digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
}

bool LoraManager::readWakeFrame(uint8_t master_mac_out[6], uint32_t timeout_ms)
{
    uint8_t buffer[sizeof(lora_wake_frame_t)]{};
    uint8_t index = 0;
    uint8_t raw_sample[32]{};
    uint8_t raw_sample_count = 0;
    uint32_t raw_total_count = 0;
    const uint32_t start_ms = millis();

    while (millis() - start_ms < timeout_ms) {
        while (LoraSerial.available()) {
            const uint8_t byte = (uint8_t)LoraSerial.read();
            raw_total_count++;
            if (raw_sample_count < sizeof(raw_sample)) {
                raw_sample[raw_sample_count++] = byte;
            }

            if (index == 0 && byte != MASTER_FRAME_HEAD) {
                continue;
            }

            buffer[index++] = byte;

            if (index == 2 && buffer[1] != FRAME_WAKE) {
                index = 0;
                continue;
            }

            if (index == sizeof(protocol_frame_t) &&
                frame_validate(buffer, sizeof(protocol_frame_t), MASTER_FRAME_HEAD, FRAME_WAKE)) {
                ESP_LOGW(LORA_TAG, "Received legacy 8-byte LoRa wake frame without master MAC");
                index = 0;
                continue;
            }

            if (index == sizeof(lora_wake_frame_t)) {
                if (lora_wake_frame_validate(buffer, sizeof(buffer))) {
                    const lora_wake_frame_t* frame = (const lora_wake_frame_t*)buffer;
                    memcpy(master_mac_out, frame->master_mac, 6);
                    ESP_LOGI(LORA_TAG, "Received %u raw LoRa bytes before valid wake frame",
                             (unsigned)raw_total_count);
                    return true;
                }

                index = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(LORA_TAG, "LoRa wake raw byte count: %u", (unsigned)raw_total_count);
    if (raw_sample_count > 0) {
        char hex[sizeof(raw_sample) * 3 + 1]{};
        for (uint8_t i = 0; i < raw_sample_count; i++) {
            snprintf(hex + (i * 3), sizeof(hex) - (i * 3), "%02X ", raw_sample[i]);
        }
        ESP_LOGW(LORA_TAG, "LoRa wake raw sample: %s", hex);
    }

    return false;
}

// ======================== 关闭 ========================

void LoraManager::shutdown()
{
    digitalWrite(LORA_POWER_PIN, LORA_POWER_OFF);
    LoraSerial.end();
}

// Lora使能ce引脚，CE低电平，进入发送数据和配置模式
void LoraManager::enable_ce()
{
    digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);
}

// Lora禁用ce引脚，CE高电平，进入接收数据模式
void LoraManager::disable_ce()
{
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
}
