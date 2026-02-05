#include <Arduino.h>
#include "pins.h"
#include "sensor.h"
#include "lora.h"
#include "config.h"
#include "radar.h"

PowerManager power;
RadarModule Radar;
LoraManager Lora;

// Lora模块和雷达模块的串口
HardwareSerial& LoraSerial = Serial1;
#ifdef OUTSIDE
    HardwareSerial& RadarSerial = Serial1;
#endif

void setup()
{
    

}

void loop()
{

}






// void setup()
// {
//     // 获取唤醒原因
//     Power.get_wakeup_reason();
//     // 初始化唤醒引脚为输入模式
//     Power.wakeup_gpio_init();
//     // 初始化lora串口和CE控制引脚
//     Lora.lora_init();
//     // 进行lora无线唤醒模式配置
//     Lora.lora_config();
//     // lora模块配置为睡眠模式
//     Lora.lora_sleep_mode();
//     // ESP32进入睡眠模式
//     Power.deep_sleep();
// }

// void loop()
// {

// }




// // PowerManager Power;

// // // 唤醒原因打印测试
// // void setup()
// // {
// //     Power.get_wakeup_reason();
// //     pinMode(WAKE_BUTTON_PIN, INPUT);
// //     pinMode(DEV_BUTTON_PIN, INPUT);
    
// //     delay(1000);

// //     printf("going to sleep\r\n");
// //     esp_deep_sleep_enable_gpio_wakeup(BIT(WAKE_BUTTON_PIN) | BIT(DEV_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
// //     esp_deep_sleep_start();
// // }

// // void loop()
// // {

// // }



// // // 电池电量采集
// // PowerManager power;
// // void setup()
// // {
// //     Serial.begin(115200);
// //     power.init();
// // }

// // void loop()
// // {
// //     uint8_t power_percent = power.get_battery_value();
// //     delay(2000);
// // }



// // // LED灯代码测试程序
// // LEDControler LED;
// // BeeperControler BEEPER;

// // void setup()
// // {
// //     LED.init();
// //     BEEPER.init();
// // }

// // void loop()
// // {
// //     BEEPER.beep(BEEPER_PERIOD_1);
// // }





// // LEDControler LED;
// // BeeperControler BEEPER;

// // void led_task(void* pvParameters)
// // {
// //   LED.init();
// //   while(true)
// //   {
// //     LED.blink(LED_PERIOD_3);
// //   }
// // }

// // void beeper_task(void* pvParameters)
// // {
// //   BEEPER.init();
// //   while(true)
// //   {
// //     BEEPER.beep(BEEPER_PERIOD_1);
// //   }
// // }

// // void monitorTask(void* pvParameters)
// // {
// //   const TickType_t xDelay = pdMS_TO_TICKS(5000);
// //   char pcBuffer[1024];
// //   while(true)
// //   {
// //     vTaskDelay(xDelay);
// //     // 任务列表打印
// //     Serial.println("\n========== Task List ==========");
// //     vTaskList(pcBuffer);
// //     Serial.print(pcBuffer);

// //     // 各任务剩余栈
// //     Serial.println("\n========== Stack HighWaterMark(words / bytes) ==========");
// //     TaskStatus_t xStatus;
// //     volatile UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
// //     TaskStatus_t* pxStatusArray = (TaskStatus_t* )pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ));
// //     if (pxStatusArray != NULL)
// //     {
// //       uxArraySize = uxTaskGetSystemState(pxStatusArray, uxArraySize, NULL);
// //       for(UBaseType_t i = 0; i < uxArraySize; i++)
// //       {
// //         UBaseType_t wm = uxTaskGetStackHighWaterMark(pxStatusArray[i].xHandle);
// //         Serial.printf("  %-16s %5u / %5u\r\n", pxStatusArray[i].pcTaskName, wm, wm * 4);
// //       }
// //       vPortFree(pxStatusArray);
// //     }

// //     // 堆信息
// //     Serial.println("\n========== Heap Information ==========");
// //     Serial.printf("Free Heap now       : %u bytes\r\n", xPortGetFreeHeapSize());
// //     Serial.printf("Min ever Free Heap  : %u bytes\r\n", xPortGetMinimumEverFreeHeapSize());
// //     Serial.println("========================================\r\n");
// //     Serial.println();
// //   }
// // }

// // void setup()
// // {
// //   Serial.begin(115200);
// //   Serial.println("Start Create Task ......");
// //   xTaskCreate(led_task, "led", 2048, NULL, 1, NULL);
// //   xTaskCreate(beeper_task, "beeper", 2048, NULL, 1, NULL);
// //   xTaskCreate(monitorTask, "Monitor", 4096, NULL, 3, NULL);

// //   vTaskDelete(NULL);
// // }


// // void loop()
// // {

// // }



// // void setup()
// // {
// //   Serial.begin(115200);
// //   // ---------------------------- 检测按键唤醒原因 ---------------------------------
// //   esp_sleep_wakeup_cause_t wakeup_reason;
// //   wakeup_reason = esp_sleep_get_wakeup_cause();
// //   switch(wakeup_reason)
// //   {
// //     case ESP_SLEEP_WAKEUP_GPIO: printf("wake caused by RTC_IO\r\n"); break;
// //     default : printf("Wakeup was not caused by deep sleep: %d\r\n",wakeup_reason); break;
// //   }
// // // ------------------------------------------------------------------------------
// //   pinMode(WAKE_BUTTON_PIN, INPUT);
// //   pinMode(DEV_BUTTON_PIN, INPUT);
// //   delay(1000);
  
// //   esp_deep_sleep_enable_gpio_wakeup(BIT(WAKE_BUTTON_PIN) | BIT(DEV_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
// //   printf("Go to sleep now\r\n");
// //   esp_deep_sleep_start();

// // }

// // void loop()
// // {

// // }