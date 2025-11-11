#include "main.h"

RTC_DATA_ATTR int bootCount = 0;
void lora_init();
void work_mode();
void config_mode(espnow_master master);
void lora_query_default_parameter();

HardwareSerial lora_serial(1);
void setup()
{

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(WAKE_PIN, INPUT);
  pinMode(LORA_CE_PIN, OUTPUT);
  digitalWrite(LORA_CE_PIN, LOW);
  lora_serial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  lora_query_default_parameter();
  // delay(1000);
  // lora_init();
  printf("Lora init success!\r\n");
  buzzer_init();
  ++bootCount;
  printf("Boot count: %d\r\n", bootCount);
  espnow_master master;
  master.wifi_init();
  uint8_t master_mac[6]{};
  WiFi.macAddress(master_mac);
  printf("Master MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",master_mac[0],master_mac[1],master_mac[2],master_mac[3],master_mac[4],master_mac[5]);

  bool slave_mac_exist = master.read_slave_mac();

  SYSTEM_MODE system_mode = NOMAL_MODE;

// ---------------------------- 检测按键唤醒原因 ---------------------------------
  // esp_sleep_wakeup_cause_t wakeup_reason;
  // wakeup_reason = esp_sleep_get_wakeup_cause();
  // switch(wakeup_reason)
  // {
  //   case ESP_SLEEP_WAKEUP_GPIO: printf("wake caused by RTC_IO"); break;
  //   default : printf("Wakeup was not caused by deep sleep: %d\r\n",wakeup_reason); break;
  // }
// ------------------------------------------------------------------------------

// -------------------------- 检测按键状态 ---------------------------------
  bool user_button_level = digitalRead(WAKE_PIN);
  
// 按键误触检测
  for(int i = 0; i < 3; i++)
  {
    user_button_level &= digitalRead(WAKE_PIN);
    delay(100);
    printf("user_button_level: %d\r\n", user_button_level);
  }

  if(user_button_level == LOW)
  {
    uint32_t user_button_start_time = millis();
    uint32_t user_button_end_time = 0;
    bool user_button_released = false;
    
    // 按键按下点亮LED，直到按键松开，指示按键按下
    digitalWrite(LED_PIN, HIGH);
    while(user_button_level == LOW)
    {
      user_button_level = digitalRead(WAKE_PIN);
      if(user_button_level == HIGH && user_button_released == false)
      {
        user_button_end_time = millis();
        user_button_released = true;
      }
      delay(300);
    }
    uint32_t user_button_press_time = 0;
    if(user_button_end_time == 0)
    {
      user_button_end_time = millis();
    }
    user_button_press_time = user_button_end_time - user_button_start_time;
    printf("User Button pressed time: %lu\r\n", user_button_press_time);

    digitalWrite(LED_PIN, LOW);
    // ---------------------------- 根据按键按下的不同时间进入不同的模式 ---------------------------------
    if(user_button_press_time > 10000 || slave_mac_exist == false)
    {
      system_mode = RECONFIG_MODE;
    }
    else if(user_button_press_time > 3000)
    {
      system_mode = TEST_MODE;
    }
    else if(user_button_press_time > 1000)
    {
      system_mode = WORK_MODE;
    }
    else
    {
      system_mode = NOMAL_MODE;
    }
  }
  printf("System mode: %d\r\n", system_mode);

  // ----------------------------根据系统模式执行对应的操作---------------------------------
  switch(system_mode)
  {
    case NOMAL_MODE:
      printf("Sysytem mode: NOMAL_MODE\r\n");
      led_blink(1);
      break;
    case WORK_MODE:
      printf("Sysytem mode: WORK_MODE\r\n");
      led_blink(2);
      work_mode();
      break;
    case TEST_MODE:
      printf("Sysytem mode: TEST_MODE\r\n");
      led_blink(3);
      test_mode();
      break;
    case RECONFIG_MODE:
      printf("Sysytem mode: RECONFIG_MODE\r\n");
      led_blink(4);
      config_mode(master);
      break;
  }
  digitalWrite(LED_PIN, LOW);

  esp_deep_sleep_enable_gpio_wakeup(BIT(WAKE_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

  // 手动关闭WIFI
  esp_wifi_stop();

  printf("Going to sleep now\r\n");
  esp_deep_sleep_start();
}


void work_mode()
{
  digitalWrite(LED_PIN, HIGH);
  uint32_t user_button_start_time = 0;
  bool user_button_pressed = false;
  while(1)
  {
    int button_state = digitalRead(WAKE_PIN);
    if(button_state == LOW && !user_button_pressed)
    {
      user_button_start_time = millis();
      user_button_pressed = true;
    }
    else if(button_state == HIGH && user_button_pressed)
    { 
      user_button_pressed = false;
    }
    if(user_button_pressed && (millis() - user_button_start_time > 3000))
    {
      digitalWrite(LED_PIN, LOW);
      break;
    }
    delay(10);
  }
}

void config_mode(espnow_master master)
{
  printf("slave mac is not exist, start config mode\r\n");
  master.delete_slave_mac();
  master.broadcast_mode_init();
  master.broadcast_mode_loop();
}

void lora_init()
{
    lora_serial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

    lora_serial.println("AT+MODE=0");
    delay(30);
    lora_serial.println("AT+RFCH=18");
    delay(30);
    lora_serial.println("AT+PID=255");
    delay(30);
    lora_serial.println("AT+MAMP=1");
    delay(30);
    lora_serial.println("AT+MLPWR=3");
    delay(30);
    lora_serial.println("AT+MID=17");
    delay(30);
    lora_serial.println("AT+MODE=1");
    delay(30);
}

void lora_query_default_parameter()
{
    lora_serial.println("AT+MODE=0");
    delay(30);
    lora_serial.println("AT+ALL");
    delay(30);
    lora_serial.println("AT+MODE=1");
    delay(30);
}

void loop()
{
  
}


