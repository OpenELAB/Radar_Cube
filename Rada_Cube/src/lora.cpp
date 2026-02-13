#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "esp_check.h"
#include "pins.h"
#include "config.h"
#include "lora.h"
#include "protocol.h"

const char* LORA_FLAG = "lora_flag";

// lora硬件串口
// HardwareSerial loraSerial(1);

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

// 判断NVS里面是否有关于Lora的key，没有新建默认为false、没有设置过lora无线唤醒模式
void LoraManager::flag_ifconfig()
{
    lora_prefe.begin("lora", false);
    bool flag = lora_prefe.isKey(LORA_FLAG);
// LoraManager的构造函数，用于创建NVS的key值
LoraManager::LoraManager()
{
    prefe.begin("lora", false);
    bool flag = prefe.isKey(LORA_FLAG);
    if(!flag)
    {
        ESP_LOGI(LORA_TAG, "lora module not config ....... ");
        lora_prefe.putBool(LORA_FLAG, false);
        prefe.putBool(LORA_FLAG, false);
    }
    else
    {
        ESP_LOGI(LORA_TAG, "lora module has configed .......");
    }
    lora_prefe.end();
    prefe.end();
}

// 配置lora串口和CE控制引脚
void LoraManager::lora_init()
{
    LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);

#ifdef INSIDE
    // 车内模块需要打开lora模块的电源，车外模块Lora模组是一直打开的
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);
#endif
}

// 发送AT指令，并等待返回指令
bool LoraManager::at_send_wait_reponse(const char* cmd, int timeout, uint8_t maxretry)
{
    // 检测AT指令是否为空
    if(!cmd)
    {
        ESP_LOGE(LORA_TAG, "cmd is null, %s\r\n", cmd);
        return false;
    }
    for(uint8_t retry = 0; retry < maxretry; retry++)
    {
        // 清空串口缓存区
        while(LoraSerial.available()) LoraSerial.read();

        // 发送AT指令
        LoraSerial.println(cmd);

        // 等待响应
        uint32_t t_start = millis();
        String response = "";
        while(millis() - t_start < timeout)
        {
            if(LoraSerial.available())
            {
                char c = LoraSerial.read();
                response += c;
                if (c == '\n')
                {
                    // 去除换行符
                    response.trim();
                    // 返回OK
                    if(response.equals("OK"))
                    {
                        ESP_LOGI(LORA_TAG, "[OK] %s", cmd);
                        return true;
                    }
                    // 返回ERROR
                    if(response.equals("ERROR"))
                    {
                        ESP_LOGE(LORA_TAG, "[ERROR] %s", cmd);
                        break;
                    }
                }
            }
        }
        // 超时
        if(millis() - t_start >= timeout)
        {
            ESP_LOGE(LORA_TAG, "[Timeout] %s\r\n", cmd);
        }
    }
    return false;
}

// 配置lora无线通信模块
bool LoraManager::lora_setting()
{

    // 判断是否是第一次配置
    bool first_config = get_lora_flag();
    
    // 如果是，配置lora为无线通信模式
    if(!first_config)
    {
        ESP_LOGI(LORA_TAG, "First config lora module, start config lora module ...... ");
        // 拉低lora模组的CE引脚
        digitalWrite(LORA_CE_PIN, GPIO_CE_INACTIVE_LEVEL);
        // 发送命令
        for(int i = 0; i < lora_init_cmd_len; i++)
        {
            if(!at_send_wait_reponse(lora_init_cmd[i], LORA_AT_TIMEOUT, LORA_AT_RETRY))
            {
                // AT指令配置失败
                ESP_LOGE(LORA_TAG, "[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
                digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
                return false;
            }
        }
        // 配置完拉高电平进入到无线唤醒模式
        digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
        // 配置完成，写入配置标志位
        lora_prefe.begin("lora", false);
        lora_prefe.putBool("lora_flag", true);
        lora_prefe.end();
    }
    // 已经配置过了，不需要再进行配置
    else
    {
        ESP_LOGI(LORA_TAG, "Lora module has been configured, no need to configure again ");

        // 测试用，不然烧录完后第一次就自动配置好了，后面日志里看不到
        // prefe.begin("lora", false);
        // prefe.remove("lora_flag");
        // prefe.end();
    }
    return true;
}

// 获取lora配置的标志位
bool LoraManager::get_lora_flag()
{
    lora_prefe.begin("lora", false);
    bool flag = lora_prefe.getBool(LORA_FLAG, false);
    lora_prefe.end();
    prefe.begin("lora", false);
    bool flag = prefe.getBool(LORA_FLAG, false);
    prefe.end();
    return flag;
}

// 写入lora配置的标志位
void LoraManager::write_lora_flag(bool flag)
{
    lora_prefe.begin("lora", false);
    lora_prefe.putBool(LORA_FLAG, flag);
    lora_prefe.end();
    prefe.begin("lora", false);
    prefe.putBool(LORA_FLAG, flag);
    prefe.end();
}

// 清除lora配置的KEY
bool LoraManager::clear_lora_key()
{
    lora_prefe.begin("lora", false);
    bool result = lora_prefe.remove(LORA_FLAG);
    lora_prefe.end();
    prefe.begin("lora", false);
    bool result = prefe.remove(LORA_FLAG);
    prefe.end();
    return result;
}


void LoraManager::lora_end()
{
    digitalWrite(LORA_POWER_PIN, LORA_POWER_OFF);
    LoraSerial.end();
}


void LoraManager::wireless_wake_up()
{
    // 主机Lora无线唤醒通信协议帧
    protocol_frame_t master_wireless_wake_frame;
    master_wireless_wake_frame.head = MASTER_FRAME_HEAD;
    master_wireless_wake_frame.type = MASTER_WIRELESS_WAKE_FRAME;
    master_wireless_wake_frame.len  = FRAME_DATA_LEN;
    // mac地址匹配帧的数据部分全为0
    master_wireless_wake_frame.dist = WIRELESS_WAKE_DATA;
    master_wireless_wake_frame.angle = WIRELESS_WAKE_DATA;
    master_wireless_wake_frame.reserve = RESERVE_DATA;
    // CRC校验值
    master_wireless_wake_frame.crc = crc_set(&master_wireless_wake_frame);

    // 发送唤醒帧
    // 拉低lora模组的CE引脚
    digitalWrite(LORA_CE_PIN, GPIO_CE_INACTIVE_LEVEL);
    LoraSerial.write((uint8_t*)&master_wireless_wake_frame, sizeof(protocol_frame_t));
    // 拉高电平
    digitalWrite(LORA_CE_PIN, GPIO_CE_ACTIVE_LEVEL);
}


void LoraManager::lora_config()
{
    lora_init();
    lora_setting();
}

