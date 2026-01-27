#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_now.h>
#include <WiFi.h>

HardwareSerial LoraSerial(1);
// AT指令发送和检测返回状态
bool send_at_wait_response(const char* cmd, int timeout = 1000);
// ESP-NOW接收回调函数
void esp_now_receive_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len);

#define LORA_RX_PIN         6
#define LORA_TX_PIN         7
#define LORA_CE_PIN         10

// 发送端的mac地址
const uint8_t send_mac[] = {0x58, 0x8C, 0x81, 0x9C, 0xD0, 0x00}; // 58:8C:81:9C:D0:00

// 第一次上电参数配置
const char* init_config_cmd[] = 
{
  "AT+MODE=0",    // 进入配置模式
  "AT+RFBR=6000", // 配置普通模式的空中传输速率6000bps
  "AT+LPWR=0",    // 关闭深度睡眠模式
  "AT+RFCH=18",   // 设置空中唤醒的频道433MHz
  "AT+PID=255",   // 设置PID255
  "AT+MAMP=2",    // 使能无线唤醒模式
  "AT+MLPWR=2",   // 使能无线唤醒模式2
  "AT+MID=17",    // 设置唤醒ID为17
  "AT+MODE=1",    // 退出配置模式
};
const int init_config_len = sizeof(init_config_cmd) / sizeof(init_config_cmd[0]);


// 普通模式配置
const char* nomal_mode_cmd[] = 
{
  "AT+MODE=0",
  "AT+MAMP=0",
  "AT+MODE=1"
};
const int nomal_mode_len = sizeof(nomal_mode_cmd) / sizeof(nomal_mode_cmd[0]);

// 无线唤醒模式配置
const char* wireless_wake_cmd[] = 
{
  "AT+MODE=0",
  "AT+MAMP=2",
  "AT+MODE=1"
};
const int wireless_wake_len = sizeof(wireless_wake_cmd) / sizeof(wireless_wake_cmd[0]);

// 接收标志位
volatile bool flag = false;
// 发送信息次数
int send_count = 0;
// 记录超时次数
int timeout_count = 0;
// 测试次数
int test_count = 0;
void setup()
{
    Serial0.begin(115200);
    LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);
    digitalWrite(LORA_CE_PIN, LOW);
    delay(1000);
    for(int i = 0; i < wireless_wake_len; i++)
    {
        send_at_wait_response(wireless_wake_cmd[i]);
    }

    // 初始化ESP-NOW，设置接收监听函数
    WiFi.mode(WIFI_STA);
    // 打印自身的mac地址
    // Serial0.print("myself mac is:");
    // Serial0.println(WiFi.macAddress());

    if(esp_now_init() != ESP_OK)
    {
        Serial0.println("Error initializing ESP-NOW");
        ESP.restart();
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, send_mac, 6);
    peer.channel = 0;
    peer.encrypt = 0;
    if(esp_now_add_peer(&peer) != ESP_OK)
    {
        Serial0.println("Failed to add peer");
    }

    esp_now_register_recv_cb(esp_now_receive_cb);
}

void loop()
{
    while(test_count < 20)
    {
        send_count++;
        test_count++;
        Serial0.printf("send_count: %d\r\n", send_count);
        flag = false;
        uint32_t send_start = millis();
        LoraSerial.printf("%d\r\n", send_count);
        // 等待espnow的接收标志位和判断超时时间
        while(!flag && millis() - send_start < 10000);
        if(millis() - send_start >= 10000)
        {
            timeout_count++;
            Serial0.printf("Time out count: %d\r\n", timeout_count);
        }
        else
        {
            Serial0.printf("consum time: %d\r\n", millis() - send_start);
            delay(2000);
        }
    }

    delay(1000);
}

// 发送AT指令，等待响应
bool send_at_wait_response(const char* cmd, int timeout)
{
    uint8_t resend_count = 0;
    while(true)
    {
        resend_count++;
        // 清除缓存区
        while(LoraSerial.available()) LoraSerial.read();

        // 发送AT指令
        LoraSerial.println(cmd);

        // 开始计时
        uint32_t t_start = millis();
        //等待返回值
        String response = "";
        while (millis() - t_start < timeout)
        {
            if(LoraSerial.available())
            {
                char c = LoraSerial.read();
                response += c;
                if(c == '\n')
                {
                    response.trim();
                    if(response.equals("OK"))
                    {
                        Serial0.printf("[OK] %s\r\n", cmd);
                        return true;
                    }
                    if(response.equals("ERROR"))
                    {
                        Serial0.printf("[ERROR] %s\r\n", cmd);
                        break;
                    }
                }
            }
        }
        if(millis() - t_start >= timeout)
        {
            Serial0.printf("TIMEOUT %s\r\n", cmd);
        }

        if(resend_count >= 10)
        {
            Serial0.printf("[FAIL] %s\r\n", cmd);
            break;
        }
    }
    return false;
}


void esp_now_receive_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if(len == sizeof(uint32_t))
    {
        uint32_t val = *(uint32_t*)data;
        Serial0.printf("ESP-NOW Received: %d\r\n", val);
        flag = true;
    }
}


