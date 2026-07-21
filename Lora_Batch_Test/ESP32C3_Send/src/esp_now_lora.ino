#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_now.h>
#include <WiFi.h>

HardwareSerial LoraSerial(1);
// AT command sending and detection return status
bool send_at_wait_response(const char* cmd, int timeout = 1000);
// ESP-NOW receives callback function
void esp_now_receive_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len);

#define LORA_RX_PIN         6
#define LORA_TX_PIN         7
#define LORA_CE_PIN         0

// The mac address of the sender
const uint8_t send_mac[] = {0x58, 0x8C, 0x81, 0x9C, 0xD0, 0x00}; // 58:8C:81:9C:D0:00

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

// receive flag bit
volatile bool flag = false;
// Number of messages sent
int send_count = 0;
// Record the number of timeouts
int timeout_count = 0;
// Number of tests
int test_count = 0;
void setup()
{
    Serial0.begin(115200);
    LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);
    digitalWrite(LORA_CE_PIN, LOW);
    for(int i = 0; i < wireless_wake_len; i++)
    {
        delay(100);
        send_at_wait_response(wireless_wake_cmd[i]);
    }

    // Initialize ESP-NOW and set up the receiving listening function
    WiFi.mode(WIFI_STA);
    // Print your own mac address
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
    while(test_count < 1000)
    {
        send_count++;
        test_count++;
        Serial0.printf("send_count: %d\r\n", send_count);
        flag = false;
        uint32_t send_start = millis();
        LoraSerial.printf("%d\r\n", send_count);
        // Waiting for espnow’s reception flag and judgment timeout
        while(!flag && millis() - send_start < 3000);
        if(millis() - send_start >= 3000)
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


void esp_now_receive_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if(len == sizeof(uint32_t))
    {
        uint32_t val = *(uint32_t*)data;
        Serial0.printf("ESP-NOW Received: %d\r\n", val);
        flag = true;
    }
}


