#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "esp_check.h"
#include "pins.h"
#include "config.h"
#include "lora.h"

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

// 配置lora串口和CE控制引脚
void LoraManager::init()
{
    loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);
}

bool LoraManager::at_send_wait_reponse(const char* cmd, int timeout, uint8_t maxretry)
{
    // 检测AT指令是否为空
    if(!cmd)
    {
        printf("[Error] cmd is null, %s\r\n", cmd);
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
                        printf("[OK] %s\r\n", cmd);
                        return true;
                    }
                    // 返回ERROR
                    if(response.equals("ERROR"))
                    {
                        printf("[ERROR] %s\r\n", cmd);
                        break;
                    }
                }
            }
        }
        // 超时
        if(millis() - t_start >= timeout)
        {
            printf("[Timeout] %s\r\n", cmd);
        }
    }
    return false;
}

// 配置lora无线通信模块
bool LoraManager::lora_config()
{
    // 判断是否是第一次配置
    prefe.begin("lora", false);
    bool flag = prefe.isKey("lora_flag");
    prefe.end();
    
    // ==================================== 理论上，待测试 ====================================
    // 如果是，配置lora为无线通信模式
    if(!flag)
    {
        printf("[Info] First config lora module, start config lora module ...... \r\n");
        // 拉低lora模组的CE引脚
        digitalWrite(LORA_CE_PIN, LOW);
        // 发送命令
        for(int i = 0; i < lora_init_cmd_len; i++)
        {
            if(!at_send_wait_reponse(lora_init_cmd[i], LORA_AT_TIMEOUT, LORA_AT_RETRY))
            {
                printf("[Error] AT cmd error, %s \r\n", lora_init_cmd[i]);
                return false;
            }
        }
        // 配置完成，写入配置标志位
        prefe.begin("lora", false);
        prefe.putBool("lora_flag", true);
        prefe.end();
    }
    // 已经配置过了，不需要再进行配置
    else
    {
        printf("[Info] LORA module has been configured, no need to configure again \r\n");

        // 测试用，烧录完就自动配置好了，日志里看不到
        // prefe.begin("lora", false);
        // prefe.remove("lora_flag");
        // prefe.end();
    }
    return true;
}

















