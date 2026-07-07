#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>

#define RGB_LED_PIN GPIO_NUM_4
#define RGB_LED_PWR_PIN GPIO_NUM_2
#define USER_BUTTON_PIN GPIO_NUM_6

constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint32_t REPORT_INTERVAL_MS = 1000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t START_ACK_TIMEOUT_MS = 2000;
constexpr uint32_t PACKET_MAGIC = 0x43574E45; // "ENWC"
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t CONTROL_BURST_COUNT = 5;
constexpr uint16_t RGB_LED_COUNT = 4;

const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum PacketType : uint8_t {
  PKT_START = 1,
  PKT_STOP = 2,
  PKT_DATA = 3,
  PKT_START_ACK = 4,
  PKT_STOP_ACK = 5,
};

enum RxState : uint8_t {
  RX_IDLE = 0,
  RX_WAIT_TX_ACK,
  RX_TESTING,
};

struct __attribute__((packed)) EspNowPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t reserved;
  uint32_t session_id;
  uint32_t packet_id;
  uint32_t timestamp_ms;
};

Adafruit_NeoPixel pixels(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;

RxState rxState = RX_IDLE;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
volatile bool startAckReceived = false;
volatile bool firstDataReceived = false;

uint32_t lastButtonChangeMs = 0;
uint32_t waitStartMs = 0;
uint32_t lastReportMs = 0;
uint32_t sessionId = 0;
uint32_t firstPacketId = 0;
uint32_t lastPacketId = 0;
uint32_t receivedPackets = 0;
uint32_t lostPackets = 0;
uint32_t duplicateOrOldPackets = 0;
uint32_t invalidPackets = 0;
uint32_t firstRxMs = 0;
uint32_t lastRxMs = 0;

void setRgb(uint8_t red, uint8_t green, uint8_t blue) {
  for (uint16_t i = 0; i < RGB_LED_COUNT; i++) {
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
  }
  pixels.show();
}

void setRgbOff() {
  pixels.clear();
  pixels.show();
}

void resetStats(uint32_t newSessionId) {
  portENTER_CRITICAL(&statsMux);
  sessionId = newSessionId;
  firstPacketId = 0;
  lastPacketId = 0;
  receivedPackets = 0;
  lostPackets = 0;
  duplicateOrOldPackets = 0;
  invalidPackets = 0;
  firstRxMs = 0;
  lastRxMs = 0;
  startAckReceived = false;
  firstDataReceived = false;
  portEXIT_CRITICAL(&statsMux);
}

bool isValidPacket(const uint8_t *data, int len, EspNowPacket &packet) {
  if (len != static_cast<int>(sizeof(EspNowPacket))) {
    portENTER_CRITICAL(&statsMux);
    invalidPackets++;
    portEXIT_CRITICAL(&statsMux);
    return false;
  }

  memcpy(&packet, data, sizeof(packet));
  if (packet.magic != PACKET_MAGIC || packet.version != PROTOCOL_VERSION) {
    portENTER_CRITICAL(&statsMux);
    invalidPackets++;
    portEXIT_CRITICAL(&statsMux);
    return false;
  }

  return true;
}

void handleDataPacket(const EspNowPacket &packet) {
  portENTER_CRITICAL(&statsMux);

  if ((rxState != RX_WAIT_TX_ACK && rxState != RX_TESTING) ||
      packet.session_id != sessionId ||
      packet.packet_id == 0) {
    portEXIT_CRITICAL(&statsMux);
    return;
  }

  firstDataReceived = true;
  const uint32_t now = millis();
  if (firstRxMs == 0) {
    firstRxMs = now;
  }
  lastRxMs = now;

  if (lastPacketId == 0) {
    firstPacketId = packet.packet_id;
    lastPacketId = packet.packet_id;
    receivedPackets++;
  } else if (packet.packet_id == lastPacketId + 1) {
    lastPacketId = packet.packet_id;
    receivedPackets++;
  } else if (packet.packet_id > lastPacketId + 1) {
    lostPackets += packet.packet_id - lastPacketId - 1;
    lastPacketId = packet.packet_id;
    receivedPackets++;
  } else {
    duplicateOrOldPackets++;
  }

  portEXIT_CRITICAL(&statsMux);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  (void)mac;
#endif
  EspNowPacket packet = {};
  if (!isValidPacket(data, len, packet)) {
    return;
  }

  if (packet.type == PKT_START_ACK && packet.session_id == sessionId) {
    startAckReceived = true;
    return;
  }

  if (packet.type == PKT_DATA) {
    handleDataPacket(packet);
  }
}

void addBroadcastPeer() {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BROADCAST_MAC, sizeof(BROADCAST_MAC));
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
    const esp_err_t err = esp_now_add_peer(&peerInfo);
    if (err != ESP_OK) {
      Serial.printf("[RX] Add peer failed: %d\r\n", err);
    }
  }
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);

  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[RX] ESP-NOW init failed, restarting...");
    delay(1000);
    ESP.restart();
  }

  esp_now_register_recv_cb(onDataRecv);
  addBroadcastPeer();
}

bool sendControlPacket(PacketType type) {
  EspNowPacket packet = {
      .magic = PACKET_MAGIC,
      .version = PROTOCOL_VERSION,
      .type = static_cast<uint8_t>(type),
      .reserved = 0,
      .session_id = sessionId,
      .packet_id = 0,
      .timestamp_ms = millis(),
  };

  const esp_err_t err = esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  if (err != ESP_OK) {
    Serial.printf("[RX] send failed type=%u err=%d\r\n", packet.type, err);
    return false;
  }
  return true;
}

void sendControlBurst(PacketType type) {
  for (uint8_t i = 0; i < CONTROL_BURST_COUNT; i++) {
    sendControlPacket(type);
    delay(20);
  }
}

float calcLossRate(uint32_t received, uint32_t lost) {
  const uint32_t expected = received + lost;
  if (expected == 0) {
    return 0.0f;
  }
  return static_cast<float>(lost) * 100.0f / static_cast<float>(expected);
}

void copyStats(uint32_t &lastId, uint32_t &received, uint32_t &lost, uint32_t &durationMs) {
  uint32_t localFirstMs = 0;
  uint32_t localLastMs = 0;

  portENTER_CRITICAL(&statsMux);
  lastId = lastPacketId;
  received = receivedPackets;
  lost = lostPackets;
  localFirstMs = firstRxMs;
  localLastMs = lastRxMs;
  portEXIT_CRITICAL(&statsMux);

  durationMs = (localFirstMs > 0 && localLastMs >= localFirstMs) ? (localLastMs - localFirstMs) : 0;
}

void printLiveStats() {
  uint32_t lastId = 0;
  uint32_t received = 0;
  uint32_t lost = 0;
  uint32_t durationMs = 0;
  copyStats(lastId, received, lost, durationMs);

  Serial.printf("[RX] id=%lu recv=%lu lost=%lu loss=%.2f%%\r\n",
                static_cast<unsigned long>(lastId),
                static_cast<unsigned long>(received),
                static_cast<unsigned long>(lost),
                calcLossRate(received, lost));
}

void printFinalStats() {
  uint32_t lastId = 0;
  uint32_t received = 0;
  uint32_t lost = 0;
  uint32_t durationMs = 0;
  copyStats(lastId, received, lost, durationMs);

  Serial.println();
  Serial.println("========== Result ==========");
  Serial.printf("Session:   %lu\r\n", static_cast<unsigned long>(sessionId));
  Serial.printf("Last ID:   %lu\r\n", static_cast<unsigned long>(lastId));
  Serial.printf("Received:  %lu\r\n", static_cast<unsigned long>(received));
  Serial.printf("Lost:      %lu\r\n", static_cast<unsigned long>(lost));
  Serial.printf("Loss Rate: %.2f%%\r\n", calcLossRate(received, lost));
  Serial.printf("Duration:  %lu ms\r\n", static_cast<unsigned long>(durationMs));
  Serial.println("============================");
}

void beginStartRequest() {
  uint32_t newSessionId = esp_random();
  if (newSessionId == 0) {
    newSessionId = millis();
  }

  resetStats(newSessionId);
  rxState = RX_WAIT_TX_ACK;
  waitStartMs = millis();
  lastReportMs = waitStartMs;
  setRgb(50, 35, 0);
  sendControlBurst(PKT_START);

  Serial.println();
  Serial.printf("[RX] START sent, waiting TX... session=%lu\r\n", static_cast<unsigned long>(sessionId));
}

void enterTesting() {
  rxState = RX_TESTING;
  setRgb(0, 80, 0);
  lastReportMs = millis();
  Serial.println("[RX] TX ready, test started");
}

void stopTest() {
  sendControlBurst(PKT_STOP);
  rxState = RX_IDLE;
  setRgbOff();
  Serial.println("[RX] STOP sent");
  printFinalStats();
  resetStats(0);
}

void failStartRequest() {
  rxState = RX_IDLE;
  setRgbOff();
  Serial.println("[RX] START failed: no response from TX");
  resetStats(0);
}

void handleButton() {
  const bool reading = digitalRead(USER_BUTTON_PIN);
  const uint32_t now = millis();

  if (reading != lastButtonReading) {
    lastButtonChangeMs = now;
    lastButtonReading = reading;
  }

  if (static_cast<uint32_t>(now - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS &&
      reading != stableButtonState) {
    stableButtonState = reading;
    if (stableButtonState == LOW) {
      if (rxState == RX_TESTING || rxState == RX_WAIT_TX_ACK) {
        stopTest();
      } else {
        beginStartRequest();
      }
    }
  }
}

void handleStartWait() {
  if (rxState != RX_WAIT_TX_ACK) {
    return;
  }

  if (startAckReceived || firstDataReceived) {
    enterTesting();
    return;
  }

  if (static_cast<uint32_t>(millis() - waitStartMs) >= START_ACK_TIMEOUT_MS) {
    failStartRequest();
  }
}

void reportIfNeeded() {
  if (rxState != RX_TESTING) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastReportMs) >= REPORT_INTERVAL_MS) {
    lastReportMs = now;
    printLiveStats();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RGB_LED_PWR_PIN, OUTPUT);
  digitalWrite(RGB_LED_PWR_PIN, HIGH);
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);

  pixels.begin();
  pixels.setBrightness(80);
  setRgbOff();

  Serial.println();
  Serial.println("ESP-NOW antenna loss test: RX");

  setupEspNow();
  Serial.printf("[RX] MAC: %s\r\n", WiFi.macAddress().c_str());
  Serial.printf("[RX] Channel: %u\r\n", ESPNOW_CHANNEL);
  Serial.println("[RX] Button: START / STOP");
}

void loop() {
  handleButton();
  handleStartWait();
  reportIfNeeded();
}
