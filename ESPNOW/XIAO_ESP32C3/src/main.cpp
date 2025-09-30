#include <Arduino.h>
#include <HardwareSerial.h>

#define BUTTON_PIN    3
HardwareSerial Lora(1);


#define FIFO_SIZE 1024
static volatile uint8_t rxbuf[FIFO_SIZE];
static volatile uint16_t wIdx = 0;
static volatile uint16_t rIdx = 0;

void IRAM_ATTR Lora_receive()
{
  while(Lora.available())
  {
    uint8_t c = Lora.read();
    uint16_t next = (wIdx + 1) & (FIFO_SIZE - 1);
    if(next != rIdx)
    {
      rxbuf[wIdx] = c;
      wIdx = next;
    }
  }
}

RTC_DATA_ATTR int Count = 0;
void setup()
{

  Serial.begin(115200);
  Lora.begin(9600, SERIAL_8N1, 20, 21);
  Lora.onReceive(Lora_receive);
  Serial.println("Start test ...");

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }

  Lora.println("AT+MODE=0");
  delay(1000);
  Lora.println("AT+RFCH=18");
  delay(1000);
  Lora.println("AT+PID=255");
  delay(1000);
  Lora.println("AT+MAMP=1");
  delay(1000);
  Lora.println("AT+MLPWR=3");
  delay(1000);
  Lora.println("AT+MID=17");
  delay(1000);
  Lora.println("AT+MODE=1");
  delay(1000);
  Serial.println("Lora set end");
  delay(5000);
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}

void loop()
{

  uint16_t local_r = rIdx;
  uint16_t local_w = wIdx;
  if(local_r != local_w)
  {
    size_t len = (local_w > local_r) ? (local_w - local_r) : (FIFO_SIZE - local_r);
    Serial.write((uint8_t*)&rxbuf[local_r], len);
    rIdx = (local_r + len) & (FIFO_SIZE - 1);
  }
  if(wIdx - rIdx > FIFO_SIZE / 2) delay(1);
}


