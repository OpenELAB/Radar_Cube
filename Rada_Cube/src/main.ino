#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "radar.h"
#include "lora.h"
#include "sensor.h"
#include "mac_match.h"
#include "protocol.h"

enum mode
{
    UNPAIRED_MODE,
    PAIRED_MODE,
    WORK_MODE,
    DELETE_MODE
};


// Lora模块和雷达模块的串口
HardwareSerial& LoraSerial = Serial1;
#ifdef INSIDE
    BeeperControler Beeper;
    bool slave_wireless_flag = false;
#endif

#ifdef OUTSIDE
    HardwareSerial& RadarSerial = Serial1;
    RadarModule Radar;
#endif

// esp-now接收回调函数
void esp_now_rece(const esp_now_recv_info_t* info, const uint8_t* data, int len);

LEDControler Led;
PowerManager Power;
LoraManager Lora;
void setup()
{
    MacMatch& matcher = MacMatch::getInstance();
    uint32_t mode = UNPAIRED_MODE;
    // 配置按键引脚
    Power.wakeup_gpio_init();
    // 获取唤醒原因
    Power.get_wakeup_reason();

    


    Led.led_init();
    Power.power_init();
    Lora.lora_config();
    if(matcher.mac_exist())
    {
        matcher.get_mac_addr();
    }


    if(digitalRead(USER_BUTTON_PIN) == USER_BUTTON_PRESSED || digitalRead(DEV_BUTTON_PIN) == DEV_BUTTON_PRESSED)
    {
        uint32_t t_start = millis();
        Led.led_on();
        bool led_flag = true;
        while(digitalRead(USER_BUTTON_PIN) == USER_BUTTON_PRESSED || digitalRead(DEV_BUTTON_PIN) == DEV_BUTTON_PRESSED)
        {
            vTaskDelay(pdMS_TO_TICKS(300));
            if(millis() - t_start > 4000 && led_flag)
            {
                Led.led_off();
                led_flag = false;
            }
            if(millis() - t_start > 8000 && !led_flag)
            {
                Led.blink(LED_PERIOD_3);
            } 
        }

        uint32_t consum_time = millis() - t_start;
        ESP_LOGI(MAIN_TAG, "consum time: %d", consum_time);

        Led.led_off();
        if(consum_time > 8000)
        {
            mode = DELETE_MODE;
        }
        else if(consum_time > 4000)
        {
            mode = PAIRED_MODE;
        }
        else if(consum_time > 1000)
        {
            mode = WORK_MODE;
        }
        else
        {
            mode = UNPAIRED_MODE;
        }

    }

    if(mode == WORK_MODE)
    {
        if(!matcher.mac_exist())
        {
            mode = UNPAIRED_MODE;
        }
    }


    switch(mode)
    {
        case DELETE_MODE:
        {
            ESP_LOGI(MAIN_TAG, "System mode is DELETE_MODE");
            for(int i = 0; i < 3; i++)
            {
                Led.blink(LED_PERIOD_3);
            }
            matcher.clear_mac_exist_flag();
            matcher.clear_mac_addr();
            break;
        }
        case UNPAIRED_MODE:
        {
            ESP_LOGI(MAIN_TAG, "System mode is UNPAIRED_MODE");
            for(int i = 0; i < 2; i++)
            {
                Led.blink(LED_PERIOD_2);
            }
            break;
        }
        case PAIRED_MODE:
        {
            ESP_LOGI(MAIN_TAG, "System mode is PAIRED_MODE");
            Led.blink(LED_PERIOD_1);
            matcher.mac_match();
            break;
        }
// =============================== 工作模式 ================================
        case WORK_MODE:
        {
            ESP_LOGI(MAIN_TAG, "System mode is WORK_MODE");
            Led.led_on();
            // 初始化ESP-NOW
            matcher.mac_init();
            // 读取NVS里的MAC地址
            uint8_t mac_addr[6]{};
            matcher.read_mac_addr(mac_addr, sizeof(mac_addr));
            esp_now_peer_info_t peer;
            memset(&peer, 0, sizeof(esp_now_peer_info_t));
            memcpy(peer.peer_addr, mac_addr, sizeof(mac_addr));
            peer.channel = 0;
            peer.encrypt = false;
            esp_now_add_peer(&peer);
#ifdef INSIDE
            // 注册接收回调函数
            esp_now_register_recv_cb(esp_now_rece);

            // Lora 发送唤醒指令
            while(!slave_wireless_flag)
            {
                Lora.wireless_wake_up();
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
#endif

#ifdef OUTSIDE
            
#endif

            Led.led_off();
            break;
        }   
    }
    Power.deep_sleep();
}

void loop()
{

}



#ifdef INSIDE
void esp_now_rece(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if(len == sizeof(protocol_frame_t))
    {
        protocol_frame_t *p = (protocol_frame_t*)data;
        if(p->head == SLAVE_FRAME_HEAD && p->type == SLAVE_WAKE_ACK_FRAME)
        {
            if(p->crc == crc_set(p))
            {
                slave_wireless_flag = true;
            }
        }
    }
}
#endif

#ifdef OUTSIDE
void esp_now_rece(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{

}
#endif
