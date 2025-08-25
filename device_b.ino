#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLEPeriodicSync.h>

BLEScan *pBLEScan;
BLEClient *pClient;

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-0987654321ab"

bool wakeupTriggered = false;
BLEAdvertisedDevice *foundDevice;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.haveManufacturerData()) {
        std::string data = advertisedDevice.getManufacturerData();
        if (data.find("WAKEUP") != std::string::npos) {
          Serial.println(">>> WAKEUP trigger received from Cube!");
          foundDevice = new BLEAdvertisedDevice(advertisedDevice);
          wakeupTriggered = true;
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  BLEDevice::init("Sensor_Node");

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
}

void loop() {
  if (!wakeupTriggered) {
    Serial.println("Scanning...");
    pBLEScan->start(2, false);
    pBLEScan->clearResults();
  } else {
    Serial.println("Connecting to Cube GATT Server...");
    pClient = BLEDevice::createClient();
    pClient->connect(foundDevice);   // 连接触发端

    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (pRemoteService) {
      BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
      if (pRemoteCharacteristic) {
        std::string value = pRemoteCharacteristic->readValue();
        Serial.print("Read GATT value: ");
        Serial.println(value.c_str());

        // 可以写回去
        pRemoteCharacteristic->writeValue("ACK from Sensor");
      }
    }
    pClient->disconnect();
    wakeupTriggered = false; // reset for next trigger
  }
  delay(1000);
}
