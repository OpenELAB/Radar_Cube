#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_now.h>
#include <WiFi.h>

HardwareSerial LoraSerial(1);
// AT指令发送和检测返回状态
bool send_at_wait_response(const char* cmd, int timeout = 1000);

#define LORA_RX_PIN     6
#define LORA_TX_PIN     7
#define LORA_CE_PIN     10
#define LORA_WAKE_PIN   3

// 接收端的mac地址
const uint8_t receive_mac[] = {0x58, 0x8C, 0x81, 0x9F, 0xDC, 0xC0};  // 58:8C:81:9F:DC:C0

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

RTC_DATA_ATTR int bootCount = 0;

void setup()
{
    Serial0.begin(115200);
    LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);


    // 初始化Lora模块
    digitalWrite(LORA_CE_PIN, LOW);
    for(int i = 0; i < init_config_len; i++)
    {
        send_at_wait_response(init_config_cmd[i]);
    }
    digitalWrite(LORA_CE_PIN, HIGH);

    // ESP-NOW 初始化
    WiFi.mode(WIFI_STA);
    Serial0.print("myself mac is:");
    Serial0.println(WiFi.macAddress());
    
    if(esp_now_init() != ESP_OK)
    {
        Serial0.println("Error initializing ESP-NOW");
        esp_restart();
    }
    // 添加接收端的mac地址
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receive_mac, 6);
    peer.channel = 0;
    peer.encrypt = 0;
    esp_now_add_peer(&peer);

    // esp-now发送醒来的次数，表示已经醒来了
    esp_now_send(receive_mac, (uint8_t*)&bootCount, sizeof(bootCount));
    Serial0.printf("bootCount: %d\r\n", bootCount);

    // bootCount++;

    // // 进入无线唤醒模式
    // digitalWrite(LORA_CE_PIN, LOW);
    // for(int i = 0; i < wireless_wake_len; i++)
    // {
    //     send_at_wait_response(wireless_wake_cmd[i]);
    // }
    // digitalWrite(LORA_CE_PIN, HIGH);

    // // 配置唤醒引脚
    // esp_deep_sleep_enable_gpio_wakeup(BIT(LORA_WAKE_PIN), ESP_GPIO_WAKEUP_GPIO_HIGH, ESP_GPIO_WAKEUP_GPIO_LOW);

    // // 进入深度睡眠
    // Serial0.println("Entering deep sleep...");
    // esp_deep_sleep_start();

}

void loop()
{

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

