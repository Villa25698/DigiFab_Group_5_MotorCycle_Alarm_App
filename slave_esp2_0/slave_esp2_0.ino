// ============================================================================
//  MC Alarm — Slave (hazard-light relay) firmware
//  Board: ESP32-C3 DevKitM-1
//
//  Receives ESP-NOW messages from the master:
//    MSG_ALARM_STATE  -> update relay blink state
//    MSG_PING         -> reply with MSG_HEARTBEAT so master sees us as online
//
//  Before flashing each slave, set NODE_ID below (1, 2, ...).
// ============================================================================

#include <esp_now.h>
#include <WiFi.h>
#include <string.h>

// ---------- CONFIGURE PER SLAVE ----------
#define NODE_ID 1        // change to 2, 3, ... for each additional slave
// -----------------------------------------

const int relayPin = 18;

// Protocol — MUST match master
#define MSG_ALARM_STATE 0x01
#define MSG_PING        0x02
#define MSG_HEARTBEAT   0x03

typedef struct __attribute__((packed)) struct_message {
    uint8_t msgType;
    bool    alarmActive;
    uint8_t nodeId;
} struct_message;

struct_message rxMsg;
struct_message txMsg;

bool shouldBlink = false;
bool masterPeerRegistered = false;
uint8_t masterMac[6];

// Non-blocking blink state
unsigned long lastBlinkToggle = 0;
const unsigned long BLINK_INTERVAL = 750;  // must match hazard cadence
bool relayOn = false;

void ensureMasterPeer(const uint8_t *mac) {
  if (masterPeerRegistered) return;
  memcpy(masterMac, mac, 6);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    masterPeerRegistered = true;
    Serial.println("Master peer registered");
  }
}

void sendHeartbeat() {
  if (!masterPeerRegistered) return;
  txMsg.msgType     = MSG_HEARTBEAT;
  txMsg.alarmActive = shouldBlink;
  txMsg.nodeId      = NODE_ID;
  esp_now_send(masterMac, (uint8_t *)&txMsg, sizeof(txMsg));
}

// Signature matches esp32 Arduino core 3.x.
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len != sizeof(struct_message)) return;
  memcpy(&rxMsg, data, sizeof(rxMsg));
  ensureMasterPeer(info->src_addr);

  switch (rxMsg.msgType) {
    case MSG_ALARM_STATE:
      shouldBlink = rxMsg.alarmActive;
      if (!shouldBlink) {
        digitalWrite(relayPin, HIGH);  // off (active-low relay)
        relayOn = false;
      }
      break;
    case MSG_PING:
      // Treat the ping's alarmActive field as an authoritative state sync too,
      // in case we missed a prior state message.
      shouldBlink = rxMsg.alarmActive;
      sendHeartbeat();
      break;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);   // off (active-low)

  WiFi.mode(WIFI_STA);

  Serial.print("Slave MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("NODE_ID: %d\n", NODE_ID);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  unsigned long now = millis();

  if (shouldBlink) {
    if (now - lastBlinkToggle >= BLINK_INTERVAL) {
      lastBlinkToggle = now;
      relayOn = !relayOn;
      digitalWrite(relayPin, relayOn ? LOW : HIGH);  // active-low
    }
  } else if (relayOn) {
    digitalWrite(relayPin, HIGH);
    relayOn = false;
  }
}
