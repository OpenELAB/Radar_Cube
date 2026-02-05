#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "esp_check.h"
#include "pins.h"
#include "config.h"
#include "lora.h"

const char* LORA_FLAG = "lora_flag";

// lora硬件串口
HardwareSerial loraSerial(1);

// lora模块的配置命令
const char* lora_init_cmd[] = 
{  
    "AT+MODE=0",
    "AT+RFCH=18",
    "AT+PID=255",
    "AT+MAMP=2",
    "AT+MLPWR=2",
    "AT+MID=17",
    "AT+MODE=1"
};
// lora模块的配置命令数量
const int lora_init_cmd_len = sizeof(lora_init_cmd) / sizeof(lora_init_cmd[0]);

#ifdef INSIDE
// lora模块睡眠模命令
const char* lora_sleep_cmd[] = 
{
    "AT+MODE=0",
    "AT+MAMP=0",        // 关闭无线唤醒模式
    "AT+LPWR=1",        // 设置为睡眠模式
    "AT+MODE=1"
};
const int lora_sleep_cmd_len = sizeof(lora_sleep_cmd) / sizeof(lora_sleep_cmd[0]);

// lora模块唤醒命令
const char* lora_wakeup_cmd[] = 
{
    "AT+MODE=0",
    "AT+MAMP=2",
    "AT+LPWR=0",
    "AT+MODE=1"
};
const int lora_wakeup_cmd_len = sizeof(lora_wakeup_cmd) / sizeof(lora_wakeup_cmd[0]);
#endif

// LoraManager的构造函数，用于创建NVS的key值
LoraManager::LoraManager()
{
    prefe.begin("lora", false);
    bool flag = prefe.isKey(LORA_FLAG);
    if(!flag)
    {
        ESP_LOGI(LORA_TAG, "lora module not config ....... \r\n");
        // printf("[Info] lora module not config ......\r\n ");
        prefe.putBool(LORA_FLAG, false);
    }
    else
    {
        ESP_LOGI(LORA_TAG, "lora module has configed .......\r\n");
        // printf("[Info] lora module has configed .......\r\n");
    }
    prefe.end();
}

// 配置lora串口和CE控制引脚
void LoraManager::lora_init()
{
    loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);
}

// 发送AT指令，并等待返回指令
bool LoraManager::at_send_wait_reponse(const char* cmd, int timeout, uint8_t maxretry)
{
    // 检测AT指令是否为空
    if(!cmd)
    {
        ESP_LOGE(LORA_TAG, "cmd is null, %s\r\n", cmd);
        // printf("[Error] cmd is null, %s\r\n", cmd);
        return false;
    }
    for(uint8_t retry = 0; retry < maxretry; retry++)
    {
        // 清空串口缓存区
        while(loraSerial.available()) loraSerial.read();

        // 发送AT指令
        loraSerial.println(cmd);

        // 等待响应
        uint32_t t_start = millis();
        String response = "";
        while(millis() - t_start < timeout)
        {
            if(loraSerial.available())
            {
                char c = loraSerial.read();
                response += c;
                if (c == '\n')
                {
                    // 去除换行符
                    response.trim();
                    // 返回OK
                    if(response.equals("OK"))
                    {
                        ESP_LOGE(LORA_TAG, "[OK] %s\r\n", cmd);
                        // printf("[OK] %s\r\n", cmd);
                        return true;
                    }
                    // 返回ERROR
                    if(response.equals("ERROR"))
                    {
                        ESP_LOGE(LORA_TAG, "[ERROR] %s\r\n", cmd);
                        // printf("[ERROR] %s\r\n", cmd);
                        break;
                    }
                }
            }
        }
        // 超时
        if(millis() - t_start >= timeout)
        {
            ESP_LOGE(LORA_TAG, "[Timeout] %s\r\n", cmd);
            // printf("[Timeout] %s\r\n", cmd);
        }
    }
    return false;
}

// 配置lora无线通信模块
bool LoraManager::lora_config()
{

    // 判断是否是第一次配置
    bool first_config = get_lora_flag();
    
    // 如果是，配置lora为无线通信模式
    if(!first_config)
    {
        ESP_LOGI(LORA_TAG, "First config lora module, start config lora module ...... \r\n");
        // printf("[Info] First config lora module, start config lora module ...... \r\n");
        // 拉低lora模组的CE引脚
        digitalWrite(LORA_CE_PIN, GPIO_CE_INACTIVE_LEVEL);
        // 发送命令
        for(int i = 0; i < lora_init_cmd_len; i++)
        {
            if(!at_send_wait_reponse(lora_init_cmd[i], LORA_AT_TIMEOUT, LORA_AT_RETRY))
            {
                // AT指令配置失败
                ESP_LOGE(LORA_TAG, "[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
                // printf("[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
                digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
                return false;
            }
        }
        // 配置完拉高电平进入到无线唤醒模式
        digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
        // 配置完成，写入配置标志位
        prefe.begin("lora", false);
        prefe.putBool("lora_flag", true);
        prefe.end();
    }
    // 已经配置过了，不需要再进行配置
    else
    {
        ESP_LOGI(LORA_TAG, "Lora module has been configured, no need to configure again \r\n");
        // printf("[Info] LORA module has been configured, no need to configure again \r\n");
        #ifdef INSIDE
        // 唤醒lora模块，并配置为无线唤醒模式
        lora_wakeup();
        #endif

        // 测试用，不然烧录完后第一次就自动配置好了，后面日志里看不到
        // prefe.begin("lora", false);
        // prefe.remove("lora_flag");
        // prefe.end();
    }
    return true;
}

#ifdef INSIDE
// 关闭睡眠模式，配置为无线唤醒模式
bool LoraManager::lora_wakeup()
{
    // 先拉低CE引脚，唤醒lora模块
    digitalWrite(LORA_CE_PIN, GPIO_CE_INACTIVE_LEVEL);
    // 关闭睡眠模式，配置为无线唤醒模式
    for(int i = 0; i < lora_wakeup_cmd_len; i++)
    {
        if(!at_send_wait_reponse(lora_wakeup_cmd[i], LORA_AT_TIMEOUT, LORA_AT_RETRY))
        {
            // AT指令配置失败
            ESP_LOGE(LORA_TAG, "[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
            // printf("[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
            digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
            return false;
        }
    }
    digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
    ESP_LOGI(LORA_TAG, "[Info] Lora module wakeup success \r\n");
    // printf("[Info] Lora module wakeup success \r\n");
    return true;
}


// ESP32C3深度睡眠前，先关闭lora的无线唤醒模式，配置为睡眠模式
bool LoraManager::lora_sleep_mode()
{
    // 拉低CE引脚，进入配置模式
    digitalWrite(LORA_CE_PIN, GPIO_CE_INACTIVE_LEVEL);
    // 发送命令
    for(int i = 0; i < lora_sleep_cmd_len; i++)
    {
        if(!at_send_wait_reponse(lora_sleep_cmd[i], LORA_AT_TIMEOUT, LORA_AT_RETRY))
        {
            // AT指令配置失败
            ESP_LOGE(LORA_TAG, "[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
            // printf("[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
            digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
            return false;
        }
    }
    // 睡眠模式配置成功，进入睡眠模式
    digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
    ESP_LOGI(LORA_TAG, "[Info] Lora module sleep mode success \r\n");
    // printf("[Info] Lora module sleep mode success \r\n");
    return true;
}
#endif

// 获取lora配置的标志位
bool LoraManager::get_lora_flag()
{
    prefe.begin("lora", false);
    bool flag = prefe.getBool(LORA_FLAG, false);
    prefe.end();
    return flag;
}

// 写入lora配置的标志位
void LoraManager::write_lora_flag(bool flag)
{
    prefe.begin("lora", false);
    prefe.putBool(LORA_FLAG, flag);
    prefe.end();
}

// 清除lora配置的KEY
bool LoraManager::clear_lora_key()
{
    prefe.begin("lora", false);
    bool result = prefe.remove(LORA_FLAG);
    prefe.end();
    return result;
}


void LoraManager::lora_end()
{
    // 取消Lora功率开关控制引脚保持高电平状态
    
}


