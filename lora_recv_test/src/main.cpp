#include<Arduino.h>
#include <HardwareSerial.h>

void lora_init(HardwareSerial& lora_serial);

#define LORA_CE    10
#define WAKE_PIN   3

HardwareSerial lora_serial(1);
void setup()
{
    lora_serial.begin(9600, SERIAL_8N1, 6, 7);
    pinMode(WAKE_PIN, INPUT);
    pinMode(LORA_CE, OUTPUT);
    digitalWrite(LORA_CE, LOW);
    lora_init(lora_serial);
    digitalWrite(LORA_CE, HIGH);
}
void loop()
{
}

void lora_init(HardwareSerial& lora_serial)
{
    delay(300);
    lora_serial.println("AT+MODE=0");
    delay(300);
    lora_serial.println("AT+VER");
    delay(300);
    lora_serial.println("AT+DEFT");
    delay(300);
    lora_serial.println("AT+MODE=0");
    delay(300);
    lora_serial.println("AT+RFCH=18");
    delay(300);
    lora_serial.println("AT+PID=255");
    delay(300);
    lora_serial.println("AT+MAMP=2");
    delay(300);
    lora_serial.println("AT+MLPWR=3");
    delay(300);
    lora_serial.println("AT+MID=17");
    delay(300);
    lora_serial.println("AT+MODE=1");
    delay(300);
}

