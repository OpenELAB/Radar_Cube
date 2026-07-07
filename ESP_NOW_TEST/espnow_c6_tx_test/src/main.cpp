#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define LED_PIN GPIO_NUM_21

constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint32_t SEND_INTERVAL_MS = 50;
constexpr uint32_t REPORT_INTERVAL_MS = 1000;
constexpr uint32_t START_BLINK_MS = 150;
constexpr uint32_t PACKET_MAGIC = 0x43574E45; // "ENWC"
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr bool LED_ACTIVE_HIGH = true;

const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum PacketType : uint8_t {
  PKT_START = 1,
  PKT_STOP = 2,
  PKT_DATA = 3,
  PKT_START_ACK = 4,
  PKT_STOP_ACK = 5,
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

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool startRequested = false;
volatile bool stopRequested = false;
volatile uint32_t requestedSessionId = 0;

bool testRunning = false;
bool ledBlinking = false;
uint32_t sessionId = 0;
uint32_t nextPacketId = 1;
uint32_t sentPackets = 0;
uint32_t sendOkCount = 0;
uint32_t sendFailCount = 0;
uint32_t invalidPackets = 0;
uint32_t lastSendMs = 0;
uint32_t lastReportMs = 0;
uint32_t ledBlinkUntilMs = 0;

void setLed(bool on) {
  digitalWrite(LED_PIN, (on == LED_ACTIVE_HIGH) ? HIGH : LOW);
}

void resetSendStats() {
  nextPacketId = 1;
  sentPackets = 0;
  sendOkCount = 0;
  sendFailCount = 0;
}

bool isValidPacket(const uint8_t *data, int len, EspNowPacket &packet) {
  if (len != static_cast<int>(sizeof(EspNowPacket))) {
    invalidPackets++;
    return false;
  }

  memcpy(&packet, data, sizeof(packet));
  if (packet.magic != PACKET_MAGIC || packet.version != PROTOCOL_VERSION) {
    invalidPackets++;
    return false;
  }

  return true;
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

  portENTER_CRITICAL(&stateMux);
  if (packet.type == PKT_START) {
    requestedSessionId = packet.session_id;
    startRequested = true;
  } else if (packet.type == PKT_STOP) {
    requestedSessionId = packet.session_id;
    stopRequested = true;
  }
  portEXIT_CRITICAL(&stateMux);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  (void)info;
#else
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  (void)mac;
#endif
  if (status == ESP_NOW_SEND_SUCCESS) {
    sendOkCount++;
  } else {
    sendFailCount++;
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
      Serial.printf("[TX] Add peer failed: %d\n", err);
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
    Serial.println("[TX] ESP-NOW init failed, restarting...");
    delay(1000);
    ESP.restart();
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);
  addBroadcastPeer();
}

bool sendPacket(PacketType type, uint32_t packetId) {
  EspNowPacket packet = {
      .magic = PACKET_MAGIC,
      .version = PROTOCOL_VERSION,
      .type = static_cast<uint8_t>(type),
      .reserved = 0,
      .session_id = sessionId,
      .packet_id = packetId,
      .timestamp_ms = millis(),
  };

  const esp_err_t err = esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  if (err != ESP_OK) {
    sendFailCount++;
    Serial.printf("[TX] send failed type=%u id=%lu err=%d\n",
                  packet.type,
                  static_cast<unsigned long>(packetId),
                  err);
    return false;
  }
  return true;
}

void sendAck(PacketType type) {
  for (uint8_t i = 0; i < 3; i++) {
    sendPacket(type, 0);
    delay(10);
  }
}

void startTest(uint32_t newSessionId) {
  sessionId = newSessionId;
  resetSendStats();
  sendAck(PKT_START_ACK);
  testRunning = true;
  ledBlinking = true;
  setLed(false);
  ledBlinkUntilMs = millis() + START_BLINK_MS;
  lastSendMs = millis();
  lastReportMs = millis();

  Serial.println();
  Serial.printf("[TX] START session=%lu interval=%lu ms\n",
                static_cast<unsigned long>(sessionId),
                static_cast<unsigned long>(SEND_INTERVAL_MS));
}

void stopTest(uint32_t stopSessionId) {
  if (stopSessionId != sessionId) {
    return;
  }

  sendAck(PKT_STOP_ACK);
  testRunning = false;
  ledBlinking = false;
  setLed(false);

  Serial.printf("[TX] STOP session=%lu sent=%lu fail=%lu\n",
                static_cast<unsigned long>(sessionId),
                static_cast<unsigned long>(sentPackets),
                static_cast<unsigned long>(sendFailCount));

  sessionId = 0;
  resetSendStats();
}

void handleControlRequests() {
  bool localStart = false;
  bool localStop = false;
  uint32_t localSession = 0;

  portENTER_CRITICAL(&stateMux);
  if (startRequested) {
    localStart = true;
    localSession = requestedSessionId;
    startRequested = false;
  }
  if (stopRequested) {
    localStop = true;
    localSession = requestedSessionId;
    stopRequested = false;
  }
  portEXIT_CRITICAL(&stateMux);

  if (localStart && localSession != 0) {
    startTest(localSession);
  }
  if (localStop) {
    stopTest(localSession);
  }
}

void handleLed() {
  if (!ledBlinking) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - ledBlinkUntilMs) >= 0) {
    ledBlinking = false;
    if (testRunning) {
      setLed(true);
    }
  }
}

void sendDataIfNeeded() {
  if (!testRunning) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastSendMs) >= SEND_INTERVAL_MS) {
    lastSendMs += SEND_INTERVAL_MS;
    const uint32_t packetId = nextPacketId++;
    if (sendPacket(PKT_DATA, packetId)) {
      sentPackets++;
    }
  }
}

void reportIfNeeded() {
  if (!testRunning) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastReportMs) >= REPORT_INTERVAL_MS) {
    lastReportMs = now;
    Serial.printf("[TX] sent=%lu id=%lu fail=%lu\n",
                  static_cast<unsigned long>(sentPackets),
                  static_cast<unsigned long>(nextPacketId - 1),
                  static_cast<unsigned long>(sendFailCount));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  Serial.println();
  Serial.println("ESP-NOW antenna loss test: TX");

  setupEspNow();
  Serial.printf("[TX] MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[TX] Channel: %u\n", ESPNOW_CHANNEL);
  Serial.println("[TX] Waiting START");
}

void loop() {
  handleControlRequests();
  handleLed();
  sendDataIfNeeded();
  reportIfNeeded();
}
