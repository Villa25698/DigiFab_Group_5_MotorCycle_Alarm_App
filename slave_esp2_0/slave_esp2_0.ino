// ============================================================================
//  MC Alarm — Slave (hazard-light relay) firmware
//  Board: ESP32-C3 DevKitM-1
//  Arduino-ESP32 core: 3.x  (callback signatures changed in 3.0)
// ============================================================================
//
//  WHAT THIS DEVICE DOES
//  ---------------------
//  This is a "dumb" relay node. It has no sensors, no buttons, no display,
//  no BLE — only a relay and an ESP-NOW radio. Its entire job is:
//
//    - listen for messages from the master
//    - flash a 5V relay (which switches the bike's hazard lights) when
//      the master says "alarm is on"
//    - reply to pings so the master knows we're alive
//
//
//  PROTOCOL (must match the master's ESP-NOW protocol exactly)
//  -----------------------------------------------------------
//
//    msgType  | direction        | meaning
//    ---------+------------------+------------------------------------------
//    0x01     | master -> slave  | "alarm is now on / off" (alarmActive)
//    0x02     | master -> slave  | "are you alive?" + state sync
//    0x03     | slave  -> master | "yes I'm alive, I am node N"
//
//
//  RELAY WIRING
//  ------------
//  Relay polarity differs between modules and how you wire the lights.
//  Don't hard-code HIGH/LOW; use the RELAY_ON / RELAY_OFF constants
//  below and adjust them once for your hardware.
//
//  Current setting matches our hardware:
//      digitalWrite(relayPin, HIGH) -> hazard lights ON
//      digitalWrite(relayPin, LOW)  -> hazard lights OFF
//
//  If your light is on at boot when you don't expect it to be, just
//  swap the values of RELAY_ON and RELAY_OFF.
//
//
//  PER-SLAVE CONFIGURATION
//  -----------------------
//  Before flashing each slave, set NODE_ID below. Each slave MUST have a
//  unique NODE_ID (1, 2, 3, …). The master uses this to track which slave
//  is online in its node-status bitmask.
//
// ============================================================================


// ----------------------------------------------------------------------------
// 1. INCLUDES
// ----------------------------------------------------------------------------
#include <esp_now.h>     // peer-to-peer wifi protocol
#include <WiFi.h>        // ESP-NOW needs the wifi radio in STA mode
#include <string.h>      // memcpy()


// ----------------------------------------------------------------------------
// 2. CONFIGURATION
// ----------------------------------------------------------------------------
// >>> CHANGE THIS PER SLAVE BOARD <<<
#define NODE_ID 1        // each slave must have a unique id (1, 2, 3, …)

// Relay control pin.
const int relayPin = 18;

// Polarity. Swap these two if the light is on at boot or doesn't switch
// when expected.
const int RELAY_ON  = HIGH;   // pin level that lights the hazards
const int RELAY_OFF = LOW;    // pin level that turns them off

// Hazard-light blink rate. 750 ms on, 750 ms off (~0.67 Hz). Matches the
// master so visually they all flash together.
const unsigned long BLINK_INTERVAL = 750;


// ----------------------------------------------------------------------------
// 3. ESP-NOW PROTOCOL  (must match master)
// ----------------------------------------------------------------------------
#define MSG_ALARM_STATE 0x01
#define MSG_PING        0x02
#define MSG_HEARTBEAT   0x03

// Same struct as the master. __attribute__((packed)) ensures byte layout
// is identical on both sides (no padding inserted by the compiler).
typedef struct __attribute__((packed)) struct_message {
    uint8_t msgType;
    bool    alarmActive;
    uint8_t nodeId;
} struct_message;

struct_message rxMsg;     // incoming buffer
struct_message txMsg;     // outgoing buffer (heartbeats)


// ----------------------------------------------------------------------------
// 4. STATE
// ----------------------------------------------------------------------------
// Whether the relay should currently be flashing. Set by master messages.
bool shouldBlink = false;

// We only register the master as a peer the first time we hear from it,
// because we don't know the master's MAC at compile time.
bool    masterPeerRegistered = false;
uint8_t masterMac[6];

// Non-blocking blink scheduler.
unsigned long lastBlinkToggle = 0;
bool          relayOn         = false;     // current physical state of the relay


// ----------------------------------------------------------------------------
// 5. HELPERS
// ----------------------------------------------------------------------------
// First time we hear from the master we capture its MAC and register it as
// an ESP-NOW peer. Without this we couldn't send anything BACK to the
// master (heartbeats would silently fail).
void ensureMasterPeer(const uint8_t *mac) {
  if (masterPeerRegistered) return;
  memcpy(masterMac, mac, 6);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;        // 0 = "current channel"
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    masterPeerRegistered = true;
    Serial.println("Master peer registered");
  }
}

// Reply to a ping with our id. The master uses this to mark us "online"
// in its node-status bitmask, which it forwards to the phone.
void sendHeartbeat() {
  if (!masterPeerRegistered) return;       // no point — we don't know who to send to
  txMsg.msgType     = MSG_HEARTBEAT;
  txMsg.alarmActive = shouldBlink;         // echo our current state for diagnostics
  txMsg.nodeId      = NODE_ID;
  esp_now_send(masterMac, (uint8_t *)&txMsg, sizeof(txMsg));
}


// ----------------------------------------------------------------------------
// 6. ESP-NOW RECEIVE CALLBACK
// ----------------------------------------------------------------------------
// Called by the wifi driver whenever an ESP-NOW packet arrives.
// Note: signature changed in Arduino-ESP32 core 3.0; old (uint8_t*, …)
// signatures will compile but never be called.
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len != sizeof(struct_message)) return;     // ignore malformed packets
  memcpy(&rxMsg, data, sizeof(rxMsg));

  // info->src_addr is the master's MAC. Save it the first time so we can
  // reply later.
  ensureMasterPeer(info->src_addr);

  switch (rxMsg.msgType) {
    case MSG_ALARM_STATE:
      shouldBlink = rxMsg.alarmActive;
      // If the master says "off", switch the relay off immediately —
      // don't wait for the next BLINK_INTERVAL tick.
      if (!shouldBlink) {
        digitalWrite(relayPin, RELAY_OFF);
        relayOn = false;
      }
      break;

    case MSG_PING:
      // Pings double as state sync — if a previous MSG_ALARM_STATE got
      // lost, the next ping will fix our state. Then send the heartbeat
      // so the master knows we're alive.
      shouldBlink = rxMsg.alarmActive;
      sendHeartbeat();
      break;
  }
}


// ----------------------------------------------------------------------------
// 7. SETUP — runs once at boot
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Relay starts OFF — hazards dark until the master tells us otherwise.
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, RELAY_OFF);

  // ESP-NOW reuses the WiFi radio. We don't connect to a router, but we
  // still need WIFI_STA mode so the radio is on.
  WiFi.mode(WIFI_STA);

  // Print our MAC and node id at boot. Copy the MAC into the master's
  // slave1[] / slave2[] arrays — that's how the master knows us.
  Serial.print("Slave MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("NODE_ID: %d\n", NODE_ID);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
}


// ----------------------------------------------------------------------------
// 8. MAIN LOOP — runs continuously
// ----------------------------------------------------------------------------
// Non-blocking blink: toggle the relay every BLINK_INTERVAL ms when the
// master says the alarm is on. When alarm goes off, force the relay off.
void loop() {
  unsigned long now = millis();

  if (shouldBlink) {
    if (now - lastBlinkToggle >= BLINK_INTERVAL) {
      lastBlinkToggle = now;
      relayOn = !relayOn;
      digitalWrite(relayPin, relayOn ? RELAY_ON : RELAY_OFF);
    }
  } else if (relayOn) {
    // shouldBlink just transitioned to false but relay is still latched on
    // — turn it off cleanly.
    digitalWrite(relayPin, RELAY_OFF);
    relayOn = false;
  }
}
