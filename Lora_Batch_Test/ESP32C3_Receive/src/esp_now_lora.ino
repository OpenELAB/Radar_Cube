#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_now.h>
#include <WiFi.h>

HardwareSerial LoraSerial(1);
// AT command sending and detection return status
bool send_at_wait_response(const char* cmd, int timeout = 1000);
void send_cb(const uint8_t* mac, esp_now_send_status_t status);

#define LORA_RX_PIN     6
#define LORA_TX_PIN     7
#define LORA_CE_PIN     10
#define LORA_WAKE_PIN   3

// The mac address of the receiving end
const uint8_t receive_mac[] = {0x58, 0x8C, 0x81, 0x9F, 0xDC, 0xC0};  // 58:8C:81:9F:DC:C0

// First power-on parameter configuration
const char* init_config_cmd[] = 
{
  "AT+MODE=0",    // Enter configuration mode
  "AT+RFBR=6000", // Configure the air transmission rate in normal mode to 6000bps
  "AT+LPWR=0",    // Turn off deep sleep mode
  "AT+RFCH=18",   // Set the channel for air wake-up to 433MHz
  "AT+PID=255",   // Set PID255
  "AT+MAMP=2",    // Enable wireless wake-up mode
  "AT+MLPWR=2",   // Enable wireless wake-up mode 2
  "AT+MID=17",    // Set wakeup ID to 17
  "AT+MODE=1",    // Exit configuration mode
};
const int init_config_len = sizeof(init_config_cmd) / sizeof(init_config_cmd[0]);


// Normal mode configuration
const char* nomal_mode_cmd[] = 
{
  "AT+MODE=0",
  "AT+MAMP=0",
  "AT+MODE=1"
};
const int nomal_mode_len = sizeof(nomal_mode_cmd) / sizeof(nomal_mode_cmd[0]);

// Wireless wake-up mode configuration
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
    // pinMode(LORA_CE_PIN, OUTPUT);

    // digitalWrite(LORA_CE_PIN, LOW);
    // for(int i = 0; i < wireless_wake_len; i++)
    // {
    //     send_at_wait_response(wireless_wake_cmd[i]);
    // }
    // digitalWrite(LORA_CE_PIN, HIGH);

    // ESP-NOW initialization
    WiFi.mode(WIFI_STA);
    // Serial0.print("myself mac is:");
    // Serial0.println(WiFi.macAddress());
    
    if(esp_now_init() != ESP_OK)
    {
        Serial0.println("Error initializing ESP-NOW");
        esp_restart();
    }
    // Add the mac address of the receiving end
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receive_mac, 6);
    peer.channel = 0;
    peer.encrypt = 0;
    esp_now_add_peer(&peer);
    // Register esp-now send callback function
    esp_now_register_send_cb(send_cb);

    // esp-now sends the number of times it wakes up, indicating that it has woken up.
    esp_now_send(receive_mac, (uint8_t*)&bootCount, sizeof(bootCount));
    Serial0.printf("=============== bootCount: %d ===============\r\n", bootCount);
    delay(1000);
    bootCount++;

    // Configure wake-up pin
    esp_deep_sleep_enable_gpio_wakeup(BIT(LORA_WAKE_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

    // Enter deep sleep
    Serial0.println("Entering deep sleep...");
    esp_deep_sleep_start();
}

void loop()
{

}


// Send AT command and wait for response
bool send_at_wait_response(const char* cmd, int timeout)
{
    uint8_t resend_count = 0;
    while(true)
    {
        resend_count++;
        // Clear cache area
        while(LoraSerial.available()) LoraSerial.read();

        // Send AT command
        LoraSerial.println(cmd);

        // Start timing
        uint32_t t_start = millis();
        //Wait for return value
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


void send_cb(const uint8_t* mac, esp_now_send_status_t status)
{
    Serial0.println(status == ESP_NOW_SEND_SUCCESS ? "Send OK" : "Send FAIL");
}
