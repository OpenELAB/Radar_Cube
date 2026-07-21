// #include <Arduino.h>
// #include <HardwareSerial.h>

// #define LORA_RX_PIN   6
// #define LORA_TX_PIN   7
// #define LORA_CE_PIN   10

// HardwareSerial LoraSerial(1);


// // First power-on parameter configuration
// const char* init_config_cmd[] = 
// {
//   "AT+MODE=0", // Enter configuration mode
//   "AT+RFBR=6000", // Configure the air transmission rate in normal mode to 6000bps
//   "AT+LPWR=0", // Turn off deep sleep mode
//   "AT+RFCH=18", //Set the air wake-up channel 433MHz
//   "AT+PID=255", // Set PID255
//   "AT+MAMP=2", // Enable wireless wake-up mode
//   "AT+MLPWR=2", // Enable wireless wake-up mode 2
//   "AT+MID=17", //Set the wake-up ID to 17
//   "AT+MODE=1", // Exit configuration mode
// };
// const int init_config_len = sizeof(init_config_cmd) / sizeof(init_config_cmd[0]);

// const char* nomal_mode_cmd[] = 
// {
//   "AT+MODE=0",
//   "AT+MAMP=0",
//   "AT+MODE=1"
// };
// const int nomal_mode_len = sizeof(nomal_mode_cmd) / sizeof(nomal_mode_cmd[0]);

// const char* wireless_wake_cmd[] = 
// {
//   "AT+MODE=0",
//   "AT+MAMP=2",
//   "AT+MODE=1"
// };
// const int wireless_wake_len = sizeof(wireless_wake_cmd) / sizeof(wireless_wake_cmd[0]);

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


// Test code
// int time_out = 5000;
// int send_count = 0;
// void setup()
// {
//   Serial0.begin(115200);
//   LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
//   pinMode(LORA_CE_PIN, OUTPUT);

//   //Initialize wireless wake-up mode and normal mode
//   // for(int i = 0; i < init_config_len; i++)
//   // {
//   //   if(!sendATAndWaitResponse(init_config_cmd[i]))
//   //   {
//   //     Serial0.printf("Failed to send AT command: %s\r\n", init_config_cmd[i]);
//   //   }
//   // }

//   //Switch to infinite wake mode
//   for(int i = 0; i < wireless_wake_len; i++)
//   {
//     if(!sendATAndWaitResponse(wireless_wake_cmd[i]))
//     {
//       Serial0.printf("Failed to send AT command: %s\r\n", wireless_wake_cmd[i]);
//     }
//   }
// }

// void loop()
// {

//   // Calculate the time when sending the message starts
//   uint32_t send_start = millis();
//   send_count++;
//   Serial0.printf("send_count: %d\r\n", send_count);

//   //Number of times to send messages
//   LoraSerial.printf("%d\r\n", send_count);

//   delay(2100);
  
//   //Switch to normal mode
//   for(int i = 0; i < nomal_mode_len; i++)
//   {
//     if(!sendATAndWaitResponse(nomal_mode_cmd[i]))
//     {
//       Serial0.printf("Failed to send AT command: %s\r\n", nomal_mode_cmd[i]);
//     }
//   }

//   //Clear the cache area
//   while(LoraSerial.available()) LoraSerial.read();

//   //Receive the returned ack
//   int received_int = 0;
//   uint32_t wait_ack_start = millis();
//   while(millis() - wait_ack_start < time_out)
//   {
//     if (LoraSerial.available() >= 4)
//     {
//       received_int = 0;
//       for (int i = 0; i < 4; i++)
//       {
//         // receive in little endian order
//         received_int |= ((uint32_t)LoraSerial.read()) << (8 * i); 
//       }
//       Serial0.printf("Received: %d\r\n", received_int);
//       break;
//     }
//   }

//   if(millis() - wait_ack_start >= time_out)
//   {
//     Serial0.printf("Timeout waiting for ack\r\n");

//   }
//   else
//   {
//     Serial0.printf("================ consum %dms =================\r\n", millis() - send_start);
//   }

//   //Switch to wireless wake-up mode
//   for(int i = 0; i < wireless_wake_len; i++)
//   {
//     if(!sendATAndWaitResponse(wireless_wake_cmd[i]))
//     {
//       Serial0.printf("Failed to send AT command: %s\r\n", wireless_wake_cmd[i]);
//     }
//   }
// }





