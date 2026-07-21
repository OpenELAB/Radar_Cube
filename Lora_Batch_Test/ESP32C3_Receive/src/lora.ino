// #include <Arduino.h>
// #include <driver/uart.h>
// #include <HardwareSerial.h>

// HardwareSerial LoraSerial(1);

// #define LORA_CE_PIN    10
// #define LORA_RX_PIN    6
// #define LORA_TX_PIN    7
// #define LORA_WAKE_PIN  3

// RTC_DATA_ATTR int bootCount = 0;

// // When powering on for the first time, configure the parameters of normal mode and air wake-up mode, and then enter wireless wake-up mode.
// const char* init_config[] = 
// {
//   "AT+MODE=0", // Enter configuration mode
//   "AT+RFBR=6000", // Configure the air transmission rate in normal mode to 6000bps
//   "AT+LPWR=0", // Turn off deep sleep mode
//   "AT+RFCH=18", //Set the air wake-up channel 433MHz
//   "AT+PID=255", // Set PID255
//   "AT+MAMP=2", // Enable wireless wake-up mode
//   "AT+MLPWR=2", // Enable wireless wake-up mode 2
//   "AT+MID=17", //Set the wake-up ID to 17
//   "AT+MODE=1" //Exit configuration mode
// };
// const int init_config_num = sizeof(init_config) / sizeof(init_config[0]);


// // After entering the normal mode, the CE pin is pulled low before configuration, the sending end CE remains low after configuration, and the receiving end CE is pulled high.
// const char* nomal_mode[] = 
// {
//   "AT+MODE=0",
//   "AT+MAMP=0",
//   "AT+MODE=1"
// };
// const int nomal_mode_num = sizeof(nomal_mode) / sizeof(nomal_mode[0]);

// // Subsequently enter the wireless wake-up mode, the CE pin is pulled low before configuration, the sender CE remains low after configuration, and the receiver CE is pulled high.
// const char* wireless_wake_cmd[] =
// {
//   "AT+MODE=0",
//   "AT+MAMP=2",
//   "AT+MODE=1"
// };
// const int wireless_wake_mum = sizeof(wireless_wake_cmd) / sizeof(wireless_wake_cmd[0]);

// //Send AT command and wait for return value
// bool sendATAndWaitResponse(const char* cmd, int timeoutms = 500, uint8_t maxRetry = 3)
// {
//   for(uint8_t retry = 0; retry < maxRetry; retry++)
//   {
//     //Clear cache area
//     while(LoraSerial.available()) LoraSerial.read();

//     //Send AT command
//     LoraSerial.println(cmd);
    
//     // Start timing
//     uint32_t t_start = millis();
    
//     // Wait for return value
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



// // // You can perform initialization configuration after powering on for the first time, and then record whether the configuration has been saved in nvs. If not, perform initialization configuration.
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

// // // Test the time required to switch to normal mode
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

// // // Test the time required to switch wireless wake-up mode
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


// // lora receives callback function
// void Lora_onReceive()
// {
//   while(LoraSerial.available())
//   {
//     Serial0.write(LoraSerial.read());
//   }
// }


// // Omits the judgment of whether it has been configured. It is used for testing. The default is configured. You can directly switch between wireless wake-up mode and normal mode.
// void setup()
// {
//   Serial0.begin(115200);
//   delay(500);
//   LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
//   pinMode(LORA_CE_PIN, OUTPUT);

//   //Record the number of wake-ups
//   Serial0.println("Boot count: " + String(bootCount));

//   // Check the reason for wake-up
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

//   //Switch to normal mode
//   digitalWrite(LORA_CE_PIN, LOW);
//   for(int i = 0; i < nomal_mode_num; i++)
//   {
//     if(!sendATAndWaitResponse(nomal_mode[i]))
//     {
//       Serial0.printf("Failed to send nomal AT command: %s\r\n", nomal_mode[i]);
//     }
//   }
//   digitalWrite(LORA_CE_PIN, HIGH);
//   // After the switch is completed, send the total number of wake-ups
//   // LoraSerial.printf("%d\r\n", bootCount);
//   // Sender
//   LoraSerial.write((uint8_t*)&bootCount, 4);
//   Serial0.printf("send bootcount: %d\r\n", bootCount);

//   // Message interval. This delay must be present. The specific value is unknown. If it is too short or does not exist, the first instruction to switch mode will be sent as a message.
//   delay(50);

//   //Switch to wireless wake-up mode
//   digitalWrite(LORA_CE_PIN, LOW);
//   for(int i = 0; i < wireless_wake_mum; i++)
//   {
//     if(!sendATAndWaitResponse(wireless_wake_cmd[i]))
//     {
//       Serial0.printf("Failed to send wireless wake AT command: %s\r\n", wireless_wake_cmd[i]);
//     }
//   }
//   digitalWrite(LORA_CE_PIN, HIGH);

//   //Configure wake-up pin
//   esp_deep_sleep_enable_gpio_wakeup(BIT(LORA_WAKE_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

//   bootCount++;

//   //Enter sleep mode
//   Serial0.println("Going to sleep now");
//   esp_deep_sleep_start();

// }

// void loop()
// {
  
// }



