#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <BLEPeriodicAdvertising.h>

#define BUTTON_PIN 0

BLEServer *pServer;
BLEAdvertising *pAdvertising;
BLEPeriodicAdvertising *pPeriodicAdv;
BLECharacteristic *pCharacteristic;

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-0987654321ab"

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  BLEDevice::init("Cube_Trigger");
  pServer = BLEDevice::createServer();

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->setValue("Idle");
  pService->start();

  // 创建广播
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setScanResponse(true);

  // 配置周期广播
  pPeriodicAdv = pAdvertising->getPeriodicAdvertising();
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Button pressed → Start periodic advertising with WAKEUP");
    
    BLEAdvertisementData advData;
    advData.setName("Cube_Trigger");
    advData.setManufacturerData("WAKEUP");  // 唤醒信号
    pAdvertising->setAdvertisementData(advData);

    pAdvertising->start();
    pPeriodicAdv->start();  // 开启 periodic advertising

    // 更新 GATT 值，等 Client 来连
    pCharacteristic->setValue("Triggered");

    delay(5000);  // 广播5秒
    pPeriodicAdv->stop();
    pAdvertising->stop();
    Serial.println("Stop advertising");
  }
  delay(100);
}
