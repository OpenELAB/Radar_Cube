// #include <Arduino.h>
// #include <HardwareSerial.h>

// #define LORA_RX_PIN   6
// #define LORA_TX_PIN   7
// #define LORA_CE_PIN   10

// HardwareSerial LoraSerial(1);


// // 第一次上电参数配置
// const char* init_config_cmd[] = 
// {
//   "AT+MODE=0",    // 进入配置模式
//   "AT+RFBR=6000", // 配置普通模式的空中传输速率6000bps
//   "AT+LPWR=0",    // 关闭深度睡眠模式
//   "AT+RFCH=18",   // 设置空中唤醒的频道433MHz
//   "AT+PID=255",   // 设置PID255
//   "AT+MAMP=2",    // 使能无线唤醒模式
//   "AT+MLPWR=2",   // 使能无线唤醒模式2
//   "AT+MID=17",    // 设置唤醒ID为17
//   "AT+MODE=1",    // 退出配置模式
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


// 测试代码
// int time_out = 5000;
// int send_count = 0;
// void setup()
// {
//   Serial0.begin(115200);
//   LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
//   pinMode(LORA_CE_PIN, OUTPUT);

//   // 初始化无线唤醒模式和普通模式
//   // for(int i = 0; i < init_config_len; i++)
//   // {
//   //   if(!sendATAndWaitResponse(init_config_cmd[i]))
//   //   {
//   //     Serial0.printf("Failed to send AT command: %s\r\n", init_config_cmd[i]);
//   //   }
//   // }

//   // 切换到无限唤醒模式
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

//   // 计算发送消息开始的时间
//   uint32_t send_start = millis();
//   send_count++;
//   Serial0.printf("send_count: %d\r\n", send_count);

//   // 发送信息的次数
//   LoraSerial.printf("%d\r\n", send_count);

//   delay(2100);
  
//   //切换到普通模式
//   for(int i = 0; i < nomal_mode_len; i++)
//   {
//     if(!sendATAndWaitResponse(nomal_mode_cmd[i]))
//     {
//       Serial0.printf("Failed to send AT command: %s\r\n", nomal_mode_cmd[i]);
//     }
//   }

//   // 清空缓存区
//   while(LoraSerial.available()) LoraSerial.read();

//   // 接收返回的ack
//   int received_int = 0;
//   uint32_t wait_ack_start = millis();
//   while(millis() - wait_ack_start < time_out)
//   {
//     if (LoraSerial.available() >= 4)
//     {
//       received_int = 0;
//       for (int i = 0; i < 4; i++)
//       {
//         // 小端序接收
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

//   // 切换到无线唤醒模式
//   for(int i = 0; i < wireless_wake_len; i++)
//   {
//     if(!sendATAndWaitResponse(wireless_wake_cmd[i]))
//     {
//       Serial0.printf("Failed to send AT command: %s\r\n", wireless_wake_cmd[i]);
//     }
//   }
// }





