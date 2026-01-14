// #include <Arduino.h>
// #include <driver/uart.h>
// #include <HardwareSerial.h>

// HardwareSerial LoraSerial(1);

// #define LORA_CE_PIN    10
// #define LORA_RX_PIN    6
// #define LORA_TX_PIN    7
// #define LORA_WAKE_PIN  3

// RTC_DATA_ATTR int bootCount = 0;

// // 第一次上电配置普通模式、空中唤醒模式的参数、然后进入到无线唤醒模式
// const char* init_config[] = 
// {
//   "AT+MODE=0",    // 进入配置模式
//   "AT+RFBR=6000", // 配置普通模式的空中传输速率6000bps
//   "AT+LPWR=0",    // 关闭深度睡眠模式
//   "AT+RFCH=18",   // 设置空中唤醒的频道433MHz
//   "AT+PID=255",   // 设置PID255
//   "AT+MAMP=2",    // 使能无线唤醒模式
//   "AT+MLPWR=2",   // 使能无线唤醒模式2
//   "AT+MID=17",    // 设置唤醒ID为17
//   "AT+MODE=1"     // 退出配置模式
// };
// const int init_config_num = sizeof(init_config) / sizeof(init_config[0]);


// // 后续进入普通模式，配置前CE引脚拉低，配置完发送端CE保持低电平，接收端CE拉高
// const char* nomal_mode[] = 
// {
//   "AT+MODE=0",
//   "AT+MAMP=0",
//   "AT+MODE=1"
// };
// const int nomal_mode_num = sizeof(nomal_mode) / sizeof(nomal_mode[0]);

// // 后续进入无线唤醒模式，配置前CE引脚拉低，配置完发送端CE保持低电平，接收端CE拉高
// const char* wireless_wake_cmd[] =
// {
//   "AT+MODE=0",
//   "AT+MAMP=2",
//   "AT+MODE=1"
// };
// const int wireless_wake_mum = sizeof(wireless_wake_cmd) / sizeof(wireless_wake_cmd[0]);

// // 发送AT指令并等待返回值
// bool sendATAndWaitResponse(const char* cmd, int timeoutms = 500, uint8_t maxRetry = 3)
// {
//   for(uint8_t retry = 0; retry < maxRetry; retry++)
//   {
//     // 清除缓存区
//     while(LoraSerial.available()) LoraSerial.read();

//     // 发送AT指令
//     LoraSerial.println(cmd);
    
//     // 开始计时
//     uint32_t t_start = millis();
    
//     // 等待返回值
//     String line = "";
//     while (millis() - t_start < timeoutms)
//     {
//       if(LoraSerial.available())
//       {
//         char c = LoraSerial.read();
//         line += c;
//         if(c == '\n')
//         {
//           line.trim();
//           if(line == "OK")
//           {
//             Serial0.printf("[OK] %s\r\n", cmd);
//             return true;
//           }
//           if(line == "ERROR")
//           {
//             Serial0.printf("[ERROR] %s\r\n", cmd);
//             break;
//           }
//         }
//       }
//     }
//     if(millis() - t_start >= timeoutms)
//     {
//       Serial0.printf("[TIMEOUT] %s\r\n", cmd);
//     }
//   }

//   Serial0.printf("[FAIL] %s after %d retries\r\n", cmd, maxRetry);
//   return false;
// }



// // // 可以第一次上电进行初始化配置，然后在nvs里记录是否有保存过配置，如果没有保存过，则进行初始化配置
// // void setup()
// // {
// //   Serial0.begin(115200);
// //   LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
// //   pinMode(LORA_CE_PIN, OUTPUT);

// //   digitalWrite(LORA_CE_PIN, LOW);
// //   uint32_t init_time_start = millis();
// //   for(int i = 0; i < init_config_num; i++)
// //   {
// //     if(!sendATAndWaitResponse(init_config[i]))
// //     {
// //       Serial0.printf("Failed to send AT command: %s\r\n", init_config[i]);
// //       return;
// //     }
// //   }
// //   Serial0.printf("Init consum time: %dms\r\n", millis() - init_time_start);
// //   digitalWrite(LORA_CE_PIN, HIGH);

// //   // 测试切换普通模式所需要的时间
// //   delay(1000);
// //   digitalWrite(LORA_CE_PIN, LOW);
// //   uint32_t nomal_mode_time_start = millis();
// //   for(int i = 0; i < nomal_mode_num; i++)
// //   {
// //     if(!sendATAndWaitResponse(nomal_mode[i]))
// //     {
// //       Serial0.printf("Failed to send nomal AT command: %s\r\n", nomal_mode[i]);
// //       return;
// //     }
// //   }
// //   Serial0.printf("Nomal mode consum time: %dms\r\n", millis() - nomal_mode_time_start);
// //   digitalWrite(LORA_CE_PIN, HIGH);

// //   // 测试切换无线唤醒模式所需要的时间
// //   delay(1000);
// //   digitalWrite(LORA_CE_PIN, LOW);
// //   uint32_t wireless_wake_time_start = millis();
// //   for(int i = 0; i < wireless_wake_mum; i++)
// //   {
// //     if(!sendATAndWaitResponse(wireless_wake_cmd[i]))
// //     {
// //       Serial0.printf("Failed to send wireless wake AT command: %s\r\n", wireless_wake_cmd[i]);
// //     }
// //   }
// //   Serial0.printf("Wireless wake consum time: %dms\r\n", millis() - wireless_wake_time_start);
// //   digitalWrite(LORA_CE_PIN, HIGH);

// //   // LoraSerial.onReceive(Lora_onReceive); 
// // }


// // void loop()
// // {

// // }


// // lora接收回调函数
// void Lora_onReceive()
// {
//   while(LoraSerial.available())
//   {
//     Serial0.write(LoraSerial.read());
//   }
// }


// // 省略了判断是否配置过，用于测试，直接默认是配置好了的，可以直接无线唤醒模式和普通模式切换
// void setup()
// {
//   Serial0.begin(115200);
//   delay(500);
//   LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
//   pinMode(LORA_CE_PIN, OUTPUT);

//   // 记录唤醒次数
//   Serial0.println("Boot count: " + String(bootCount));

//   // 检查唤醒原因
//   esp_sleep_wakeup_cause_t wakeup_reason;
//   wakeup_reason = esp_sleep_get_wakeup_cause();
//   switch(wakeup_reason)
//   {
//     case ESP_SLEEP_WAKEUP_EXT0:
//       Serial0.println("Wakeup caused by external signal using RTC_IO");
//       break;
//     case ESP_SLEEP_WAKEUP_EXT1:
//       Serial0.println("Wakeup caused by external signal using RTC_CNTL");
//       break;
//     case ESP_SLEEP_WAKEUP_TIMER:
//       Serial0.println("Wakeup caused by timer");
//       break;
//     default:
//       Serial0.printf("Wakeup by unknown reason: %d\r\n", wakeup_reason);
//       break;
//   }

//   // 切换到普通模式
//   digitalWrite(LORA_CE_PIN, LOW);
//   for(int i = 0; i < nomal_mode_num; i++)
//   {
//     if(!sendATAndWaitResponse(nomal_mode[i]))
//     {
//       Serial0.printf("Failed to send nomal AT command: %s\r\n", nomal_mode[i]);
//     }
//   }
//   digitalWrite(LORA_CE_PIN, HIGH);
//   // 切换完成后，发送总共醒来的次数
//   // LoraSerial.printf("%d\r\n", bootCount);
//   // 发送端
//   LoraSerial.write((uint8_t*)&bootCount, 4);
//   Serial0.printf("send bootcount: %d\r\n", bootCount);

//   // 消息间隔，这个延时一定要有，具体值未知，太短或者没有会把切换模式的第一条指令当作信息发出去
//   delay(50);

//   // 切换到无线唤醒模式
//   digitalWrite(LORA_CE_PIN, LOW);
//   for(int i = 0; i < wireless_wake_mum; i++)
//   {
//     if(!sendATAndWaitResponse(wireless_wake_cmd[i]))
//     {
//       Serial0.printf("Failed to send wireless wake AT command: %s\r\n", wireless_wake_cmd[i]);
//     }
//   }
//   digitalWrite(LORA_CE_PIN, HIGH);

//   // 配置唤醒引脚
//   esp_deep_sleep_enable_gpio_wakeup(BIT(LORA_WAKE_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

//   bootCount++;

//   // 进入睡眠模式
//   Serial0.println("Going to sleep now");
//   esp_deep_sleep_start();

// }

// void loop()
// {
  
// }



