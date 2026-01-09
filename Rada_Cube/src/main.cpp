#include <Arduino.h>

#include "pins.h"
#include "sensor.h"
#include "config.h"




// 测试程序
LEDControler LED;
BeeperControler BEEPER;

void led_task(void* pvParameters)
{
  LED.init();
  while(true)
  {
    LED.blink(LED_PERIOD_3);
  }
}

void beeper_task(void* pvParameters)
{
  BEEPER.init();
  while(true)
  {
    BEEPER.beep(BEEPER_PERIOD_1);
  }
}

void monitorTask(void* pvParameters)
{
  const TickType_t xDelay = pdMS_TO_TICKS(5000);
  char pcBuffer[1024];
  while(true)
  {
    vTaskDelay(xDelay);
    // 任务列表打印
    Serial.println("\n========== Task List ==========");
    vTaskList(pcBuffer);
    Serial.print(pcBuffer);

    // 各任务剩余栈
    Serial.println("\n========== Stack HighWaterMark(words / bytes) ==========");
    TaskStatus_t xStatus;
    volatile UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t* pxStatusArray = (TaskStatus_t* )pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ));
    if (pxStatusArray != NULL)
    {
      uxArraySize = uxTaskGetSystemState(pxStatusArray, uxArraySize, NULL);
      for(UBaseType_t i = 0; i < uxArraySize; i++)
      {
        UBaseType_t wm = uxTaskGetStackHighWaterMark(pxStatusArray[i].xHandle);
        Serial.printf("  %-16s %5u / %5u\r\n", pxStatusArray[i].pcTaskName, wm, wm * 4);
      }
      vPortFree(pxStatusArray);
    }

    // 堆信息
    Serial.println("\n========== Heap Information ==========");
    Serial.printf("Free Heap now       : %u bytes\r\n", xPortGetFreeHeapSize());
    Serial.printf("Min ever Free Heap  : %u bytes\r\n", xPortGetMinimumEverFreeHeapSize());
    Serial.println("========================================\r\n");
    Serial.println();
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Start Create Task ......");
  xTaskCreate(led_task, "led", 2048, NULL, 1, NULL);
  xTaskCreate(beeper_task, "beeper", 2048, NULL, 1, NULL);
  xTaskCreate(monitorTask, "Monitor", 4096, NULL, 3, NULL);

  vTaskDelete(NULL);
}


void loop()
{

}



// void setup()
// {
//   Serial.begin(115200);
//   // ---------------------------- 检测按键唤醒原因 ---------------------------------
//   esp_sleep_wakeup_cause_t wakeup_reason;
//   wakeup_reason = esp_sleep_get_wakeup_cause();
//   switch(wakeup_reason)
//   {
//     case ESP_SLEEP_WAKEUP_GPIO: printf("wake caused by RTC_IO"); break;
//     default : printf("Wakeup was not caused by deep sleep: %d\r\n",wakeup_reason); break;
//   }
// // ------------------------------------------------------------------------------


//   esp_deep_sleep_enable_gpio_wakeup(BIT(5), ESP_GPIO_WAKEUP_GPIO_LOW);
//   esp_deep_sleep_start();

// }

// void loop()
// {

// }