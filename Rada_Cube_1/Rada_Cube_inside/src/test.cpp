// #include <Arduino.h>
// #include "pins.h"
// #include "sensor.h"
// #include "lora.h"
// #include "config.h"
// #include "radar.h"
// #include "mac_match.h"

// MacMatch mac;

// PowerManager power;
// RadarModule Radar;
// LoraManager Lora;

// //Serial port of Lora module and radar module
// HardwareSerial& LoraSerial = Serial1;
// #ifdef OUTSIDE
//     HardwareSerial& RadarSerial = Serial1;
// #endif

// void setup()
// {
//     mac.mac_match();
    

// }

// void loop()
// {

// }








// void setup()
// {
//     // Get the wake-up reason
//     Power.get_wakeup_reason();
//     //Initialize the wake-up pin to input mode
//     Power.wakeup_gpio_init();
//     //Initialize lora serial port and CE control pin
//     Lora.lora_init();
//     // Configure lora wireless wake-up mode
//     Lora.lora_config();
//     // The lora module is configured in sleep mode
//     Lora.lora_sleep_mode();
//     // ESP32 enters sleep mode
//     Power.deep_sleep();
// }

// void loop()
// {

// }




// // PowerManager Power;

// // // Wake-up reason printing test
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



// // // Battery power collection
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



// // // LED light code test program
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
// // // Task list printing
// //     Serial.println("\n========== Task List ==========");
// //     vTaskList(pcBuffer);
// //     Serial.print(pcBuffer);

// // // Remaining stack of each task
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

// // // Heap information
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
// // // ------------------------------- Detect key wake-up reasons ----------------------------------
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