// ============================================================================
//  MC Alarm — Master (Gateway) firmware
//  Board: ESP32-C3 DevKitM-1
//  Arduino-ESP32 core: 3.x  (callback signatures changed in 3.0)
// ============================================================================
//
//  WHAT THIS DEVICE DOES
//  ---------------------
//  This is the "brain" of the motorcycle alarm system. It is the only board
//  on the bike that has sensors and outputs. It does four things at once:
//
//    1. Watches the MPU-6050 gyro for a death-wobble pattern.
//    2. When a wobble fires it sounds a siren on the speaker and lights a
//       hazard triangle on an 8x8 LED matrix.
//    3. Sends an "alarm on" message to the slave board over ESP-NOW so the
//       slave can switch on the bike's hazard lights via a relay.
//    4. Talks to the rider's phone (Flutter app) over BLE so the phone can
//       show what's happening and let the rider stop the alarm.
//
//
//  HOW DATA FLOWS
//  --------------
//
//      gyro         buttons        LED matrix     speaker
//        |             |               ^             ^
//        v             v               |             |
//      ┌───────────────────────────────────────────────┐
//      │              MASTER (this code)              │
//      └─────────────┬───────────────────┬─────────────┘
//                    │                   │
//                BLE GATT            ESP-NOW
//                    │                   │
//                    v                   v
//                  PHONE              SLAVE  ─►  bike hazard relay
//
//
//  BLE GATT LAYOUT
//  ---------------
//  Service UUID                a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001
//
//  Characteristic    Suffix    Properties     Payload
//  ----------------  --------  -------------  ------------------------------
//  alarmState        ...0002   READ, NOTIFY   uint8  0=off, 1=on
//  wobbleEvent       ...0003   READ, NOTIFY   uint8  counter (++ every wobble)
//  command           ...0004   WRITE          uint8  0=stop, 1=manual on,
//                                                    2=toggle
//  nodeStatus        ...0005   READ, NOTIFY   uint8  bitmask, bit n = slave
//                                                    n+1 online
//
//
//  ESP-NOW MESSAGE
//  ---------------
//  Master and slave share this 3-byte struct (see struct_message). msgType
//  tells the receiver what the message means:
//
//    0x01 MSG_ALARM_STATE  master -> slave : "alarm is now on/off"
//    0x02 MSG_PING         master -> slave : "are you alive?"
//    0x03 MSG_HEARTBEAT    slave  -> master: "yes I'm alive, I am node N"
//
// ============================================================================


// ----------------------------------------------------------------------------
// 1. INCLUDES
// ----------------------------------------------------------------------------
#include <esp_now.h>                  // peer-to-peer wifi protocol used to talk to the slave
#include <WiFi.h>                     // ESP-NOW needs the wifi radio to be in STA mode
#include <Adafruit_MPU6050.h>         // gyro + accel driver
#include <Adafruit_Sensor.h>          // base class used by MPU6050 (sensors_event_t)
#include <Wire.h>                     // I2C driver — both gyro and matrix use it
#include <Adafruit_GFX.h>             // graphics primitives (drawLine etc.) for the matrix
#include "Adafruit_LEDBackpack.h"     // driver for the HT16K33 chip on the matrix module

#include <BLEDevice.h>                // BLE stack (we act as a GATT server)
#include <BLEServer.h>                // BLEServer + BLEServerCallbacks
#include <BLEUtils.h>                 // misc helpers used by the BLE stack
#include <BLE2902.h>                  // CCC descriptor — needed for any NOTIFY characteristic


// ----------------------------------------------------------------------------
// 2. PIN CONFIGURATION
// ----------------------------------------------------------------------------
// Buttons use INPUT_PULLUP — wiring is:
//     button leg 1 -> GPIO pin
//     button leg 2 -> GND
// Pressed = pin reads LOW, released = pin reads HIGH (held high by internal pull-up).
const int TEST_BUTTON_PIN = 4;        // press to manually fire the alarm (for demos)
const int OFF_BUTTON_PIN  = 5;        // press to silence the alarm

const int speakerPin      = 7;        // siren — driven with tone() to make a square wave

// MPU-6050 (I2C addr 0x68) and the LED matrix (0x70) live on the SAME I2C bus.
// I2C is multi-drop: many devices, two wires, addresses tell them apart.
const int I2C_SDA         = 2;
const int I2C_SCL         = 3;


// ----------------------------------------------------------------------------
// 3. ALARM STATE
// ----------------------------------------------------------------------------
//
// The alarm has THREE possible states, in order:
//
//   1. IDLE                alarmActive = false, wobblePending = false
//      Wobble detection is running. Nothing is sounding.
//
//   2. PENDING             alarmActive = false, wobblePending = true
//      A wobble was just detected. The phone has already been notified and
//      is showing the 5-second countdown. The siren has NOT fired yet.
//      The rider can press OFF (local button) or STOP (in app) to abort.
//      If 5 seconds pass with no abort, we transition to ACTIVE.
//
//   3. ACTIVE              alarmActive = true,  wobblePending = false
//      Siren on, matrix flashing, slave told to flash hazards. Stays here
//      until OFF button or app STOP.
//
// After leaving ACTIVE we enter a 3-second cooldown where we ignore the
// gyro completely — otherwise vibration from the speaker that just stopped
// would re-trigger the wobble detector immediately.
//
bool alarmActive = false;
bool wobblePending = false;
unsigned long wobblePendingUntil = 0;       // millis() value at which pending becomes ACTIVE
const unsigned long WOBBLE_GRACE_MS = 5000; // length of the pending window

unsigned long alarmStoppedAt = 0;           // millis() value of the last off-transition (0 = never)
const unsigned long POST_ALARM_COOLDOWN = 3000;  // ignore gyro for this long after stop

// At boot, the gyro takes a moment to warm up and stabilize. Don't even
// look at it for the first few seconds.
const unsigned long STARTUP_GRACE_MS = 3000;

// Set to true only if the MPU responded on I2C at boot. If it's false (bad
// wiring, dead chip, …) wobble detection is disabled entirely so we can't
// false-fire on garbage memory readings from the unconnected sensor.
bool mpuOk = false;

// Counts every wobble detection ever. Sent to the phone over BLE so the
// app can tell "this is a new wobble" vs "I'm just seeing the cached value
// after reconnect" by checking whether the counter changed.
uint8_t wobbleCounter = 0;


// ----------------------------------------------------------------------------
// 4. SENSOR / DISPLAY OBJECTS
// ----------------------------------------------------------------------------
Adafruit_MPU6050 mpu;
Adafruit_8x8matrix matrix = Adafruit_8x8matrix();

// Matrix blink scheduling. We use millis() instead of delay() so the rest
// of the loop (BLE, ESP-NOW, buttons) keeps running while the triangle blinks.
unsigned long lastBlinkTime = 0;
const int     blinkInterval = 400;          // ms — half period (on for 400 ms, off for 400 ms)
bool matrixState = false;                   // true = triangle showing, false = blank


// ----------------------------------------------------------------------------
// 5. ESP-NOW PROTOCOL (master <-> slave)
// ----------------------------------------------------------------------------
#define MSG_ALARM_STATE 0x01
#define MSG_PING        0x02
#define MSG_HEARTBEAT   0x03

// __attribute__((packed)) tells the compiler not to insert padding bytes.
// Both master and slave see the exact same 3-byte layout in memory.
typedef struct __attribute__((packed)) struct_message {
    uint8_t msgType;        // one of MSG_* above
    bool    alarmActive;    // for MSG_ALARM_STATE; also echoed in MSG_PING
    uint8_t nodeId;         // master sends 0; slave reports its own NODE_ID in heartbeats
} struct_message;

struct_message myData;                  // outgoing buffer (re-used)
esp_now_peer_info_t peerInfo;           // used during esp_now_add_peer()

// Hard-coded slave MAC. Read it once from the slave's Serial Monitor at boot
// and put it here. To support more slaves, add slave2[] / slave3[] and bump
// NUM_SLAVES.
uint8_t slave1[] = {0xAC, 0xEB, 0xE6, 0x80, 0xF3, 0xE4};
#define NUM_SLAVES 1

// For each slave we remember when its last heartbeat arrived. If it's been
// longer than HEARTBEAT_TIMEOUT we treat the slave as offline.
unsigned long lastHeartbeat[NUM_SLAVES] = {0};
const unsigned long HEARTBEAT_TIMEOUT   = 5000;   // ms
const unsigned long PING_INTERVAL       = 1500;   // ms — how often we ping each slave
unsigned long lastPingSent = 0;
uint8_t lastNodeStatus = 0xFF;          // sentinel — forces a publish on first loop


// ----------------------------------------------------------------------------
// 6. BLE CONSTANTS
// ----------------------------------------------------------------------------
// All UUIDs share the prefix a8a9b000-…-2b0f3a7c000X so they group nicely
// in BLE scanners.
#define SVC_UUID         "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001"
#define CHAR_ALARM_UUID  "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0002"
#define CHAR_WOBBLE_UUID "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0003"
#define CHAR_CMD_UUID    "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0004"
#define CHAR_NODES_UUID  "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0005"

// Pointers — actual objects are created in setup().
BLECharacteristic *chrAlarm  = nullptr;     // alarm on/off, master->phone
BLECharacteristic *chrWobble = nullptr;     // wobble counter, master->phone
BLECharacteristic *chrCmd    = nullptr;     // commands, phone->master
BLECharacteristic *chrNodes  = nullptr;     // slave online bitmask, master->phone

bool bleClientConnected = false;            // updated by ServerCallbacks


// ----------------------------------------------------------------------------
// 7. FORWARD DECLARATIONS
// ----------------------------------------------------------------------------
// These exist because the BLE callback classes (defined just below) call
// functions that are written further down in the file. C/C++ requires us
// to either declare them up front or move the definitions above the use.
void setAlarm(bool on, const char *reason);
void publishAlarmState();
void publishNodeStatus(uint8_t mask);


// ----------------------------------------------------------------------------
// 8. BLE CALLBACKS
// ----------------------------------------------------------------------------
// Fires when a phone connects or disconnects.
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleClientConnected = true;
    Serial.println("BLE: phone connected");
  }
  void onDisconnect(BLEServer*) override {
    bleClientConnected = false;
    Serial.println("BLE: phone disconnected, restarting advertising");
    // After a disconnect the BLE stack stops advertising automatically. We
    // need to restart it so the phone can re-find us next time.
    BLEDevice::startAdvertising();
  }
};

// Fires when the phone WRITES to the command characteristic (one-byte cmd).
class CmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (v.length() < 1) return;
    uint8_t cmd = (uint8_t)v[0];
    Serial.printf("BLE cmd received: %u\n", cmd);
    switch (cmd) {
      case 0:    // stop
        // STOP also has to abort a wobble that's in its grace period —
        // otherwise the rider thinks they cancelled it but the alarm
        // fires anyway after 5 seconds.
        if (wobblePending) {
          wobblePending  = false;
          alarmStoppedAt = millis();      // start the cooldown
          Serial.println("Wobble cancelled by app during grace period");
        }
        setAlarm(false, "app stop");
        break;
      case 1:    // manual on
        setAlarm(true, "app manual");
        break;
      case 2:    // toggle
        setAlarm(!alarmActive, "app toggle");
        break;
    }
  }
};


// ----------------------------------------------------------------------------
// 9. ESP-NOW CALLBACKS
// ----------------------------------------------------------------------------
// Note: signatures changed in Arduino-ESP32 core 3.0. The old (uint8_t* mac, …)
// signatures will compile but never be called by the new core.

// Called after every esp_now_send() — used only for diagnostics.
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) Serial.println("ESP-NOW send failed");
}

// Called whenever an ESP-NOW packet arrives. We only care about heartbeats
// from slaves.
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len != sizeof(struct_message)) return;     // wrong size = not our message
  struct_message incoming;
  memcpy(&incoming, data, sizeof(incoming));
  if (incoming.msgType == MSG_HEARTBEAT) {
    uint8_t id = incoming.nodeId;
    if (id >= 1 && id <= NUM_SLAVES) {
      lastHeartbeat[id - 1] = millis();
    }
  }
}


// ----------------------------------------------------------------------------
// 10. ESP-NOW SENDERS  (master -> slaves)
// ----------------------------------------------------------------------------
// Tell every registered slave whether the alarm is on or off.
void sendAlarmState(bool state) {
  myData.msgType     = MSG_ALARM_STATE;
  myData.alarmActive = state;
  myData.nodeId      = 0;
  esp_now_send(0, (uint8_t *)&myData, sizeof(myData));   // 0 = broadcast to all peers
}

// "Are you alive?" message. Slave replies with MSG_HEARTBEAT.
void sendPing() {
  myData.msgType     = MSG_PING;
  myData.alarmActive = alarmActive;     // also use the ping to (re)sync state
  myData.nodeId      = 0;
  esp_now_send(0, (uint8_t *)&myData, sizeof(myData));
}


// ----------------------------------------------------------------------------
// 11. BLE PUBLISHERS  (master -> phone)
// ----------------------------------------------------------------------------
// Each helper writes a 1-byte value into the characteristic, then NOTIFIES
// any connected phone. setValue() updates the cache (so a later READ
// returns the right value even if the phone wasn't subscribed).
void publishAlarmState() {
  if (!chrAlarm) return;
  uint8_t v = alarmActive ? 1 : 0;
  chrAlarm->setValue(&v, 1);
  if (bleClientConnected) chrAlarm->notify();
}

void publishWobble() {
  if (!chrWobble) return;
  chrWobble->setValue(&wobbleCounter, 1);
  if (bleClientConnected) chrWobble->notify();
}

void publishNodeStatus(uint8_t mask) {
  if (!chrNodes) return;
  chrNodes->setValue(&mask, 1);
  if (bleClientConnected) chrNodes->notify();
}


// ----------------------------------------------------------------------------
// 12. CENTRAL ALARM-STATE TRANSITION
// ----------------------------------------------------------------------------
// EVERY alarm on/off change goes through this one function. That way the
// siren, the matrix, the slave, and the phone always agree. If you call
// tone() / digitalWrite() / sendAlarmState() yourself somewhere else, you
// will get out-of-sync state — don't do it.
void setAlarm(bool on, const char *reason) {
  if (alarmActive == on) return;        // nothing to do — already in this state
  alarmActive = on;

  if (on) {
    tone(speakerPin, 100);              // siren tone — change frequency for a different pitch
    lastBlinkTime = millis();           // reset the blink timer so the matrix starts ON immediately
  } else {
    noTone(speakerPin);                 // stop the PWM
    digitalWrite(speakerPin, LOW);      // belt-and-suspenders — make sure the pin is silent
    matrix.clear();
    matrix.writeDisplay();
    alarmStoppedAt = millis();          // start the cooldown for the wobble detector
  }

  // Tell the slave and the phone about the change.
  sendAlarmState(on);
  publishAlarmState();
  Serial.printf("Alarm %s (%s)\n", on ? "ON" : "OFF", reason);
}


// ----------------------------------------------------------------------------
// 13. LED MATRIX — HAZARD TRIANGLE
// ----------------------------------------------------------------------------
// Draws a simple triangle outline pointing up. Coordinates are (x, y) on
// the 8x8 grid, (0,0) is the top-left corner.
void drawTriangle() {
  matrix.clear();
  matrix.drawLine(3, 0, 4, 0, LED_ON);  // top apex (2 pixels wide)
  matrix.drawLine(3, 0, 0, 7, LED_ON);  // left side
  matrix.drawLine(4, 0, 7, 7, LED_ON);  // right side
  matrix.drawLine(0, 7, 7, 7, LED_ON);  // base
  matrix.writeDisplay();
}


// ----------------------------------------------------------------------------
// 14. WOBBLE DETECTOR
// ----------------------------------------------------------------------------
// A "death wobble" on a motorcycle is the front wheel oscillating rapidly
// left-right. On the gyro that shows up as fast sign-flips of the angular
// velocity — left, right, left, right, all above some speed.
//
// Algorithm:
//   1. Read all three gyro axes. Pick whichever has the biggest magnitude
//      this sample (so the board's mounting orientation doesn't matter).
//   2. If that value AND the previous value are both above THRESHOLD, and
//      they have opposite signs, count it as a "flip".
//   3. If we collect FLIPS_REQUIRED flips within FLIP_WINDOW milliseconds
//      of each other, return true → wobble detected.
//   4. Otherwise reset the count and keep watching.
//
// Tuning: thresholds are LOOSE for demo (so a hand shake triggers it).
// On a real bike, raise THRESHOLD to ~2.5 and FLIPS_REQUIRED to 4 so road
// bumps don't false-fire.
bool checkForWobble() {
  // Read all sensors. We only use g.gyro.{x,y,z}; a (accelerometer) and
  // temp are read in the same call — the library doesn't expose a gyro-only
  // read function.
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Pick the strongest axis this sample. abs values for comparison.
  float gx = g.gyro.x, gy = g.gyro.y, gz = g.gyro.z;
  float ax = fabs(gx), ay = fabs(gy), az = fabs(gz);
  float currentRotation = gz;
  if      (ax >= ay && ax >= az) currentRotation = gx;
  else if (ay >= ax && ay >= az) currentRotation = gy;

  // ----- DEMO TUNING -----
  // For real bike use, raise THRESHOLD to ~2.5 and FLIPS_REQUIRED to 4.
  const float THRESHOLD       = 1.0;     // rad/s — minimum twist speed
  const int   FLIPS_REQUIRED  = 3;       // direction reversals needed
  const unsigned long FLIP_WINDOW = 800; // ms — flip count resets if no new flip in this long

  // These have to be static so they survive between calls — they're the
  // detector's running state.
  static float          lastRotation  = 0;
  static int            flipCount     = 0;
  static unsigned long  lastFlipTime  = 0;
  static unsigned long  lastDebug     = 0;
  static unsigned long  lastCallTime  = 0;

  // If this function hasn't been called in a while (e.g. the cooldown
  // suppressed it for 3 seconds), the static state is stale — wipe it so
  // the gap doesn't count as one giant flip.
  unsigned long nowMs = millis();
  if (lastCallTime != 0 && nowMs - lastCallTime > 500) {
    lastRotation = 0;
    flipCount    = 0;
    lastFlipTime = nowMs;
  }
  lastCallTime = nowMs;

  // Sanity floor. The gyro is configured to ±500°/s = ±8.73 rad/s. Any
  // reading bigger than that is impossible — it means I2C returned garbage
  // (broken wire, dead chip, glitch). Drop those samples on the floor.
  const float SANITY_MAX = 9.0;
  if (fabs(currentRotation) > SANITY_MAX) {
    lastRotation = 0;
    return false;
  }

  // Count a sign-change as a flip whenever the current sample clears the
  // threshold. (We used to also require the previous sample to clear the
  // threshold — that was a guard against garbage data from a disconnected
  // MPU. With the wiring fixed and SANITY_MAX catching bad samples
  // upstream, the extra check just made the detector slower to start.)
  if (fabs(currentRotation) > THRESHOLD) {
    bool signChange = (currentRotation > 0 && lastRotation < 0) ||
                      (currentRotation < 0 && lastRotation > 0);
    if (signChange) {
      flipCount++;
      lastFlipTime = millis();
      Serial.printf("  flip %d  (rot=%.2f rad/s)\n", flipCount, currentRotation);
    }
  }
  // Stale flips age out after FLIP_WINDOW so a slow wiggle doesn't
  // accumulate forever.
  if (millis() - lastFlipTime > FLIP_WINDOW) flipCount = 0;
  lastRotation = currentRotation;

  // Debug print 5×/sec so you can see live values in the Serial Monitor.
  if (millis() - lastDebug > 200) {
    lastDebug = millis();
    Serial.printf("gyro x=%.2f  y=%.2f  z=%.2f  pick=%.2f  flips=%d\n",
                  gx, gy, gz, currentRotation, flipCount);
  }

  return (flipCount >= FLIPS_REQUIRED);
}


// ----------------------------------------------------------------------------
// 15. SETUP — runs once at boot
// ----------------------------------------------------------------------------
void setup() {
  // ---- silence the speaker IMMEDIATELY ----
  // Until pinMode runs, GPIO 7 is floating. Some speaker amps will sing on
  // a floating pin. Driving it LOW within microseconds of boot stops that.
  pinMode(speakerPin, OUTPUT);
  digitalWrite(speakerPin, LOW);
  noTone(speakerPin);

  Serial.begin(115200);
  delay(1000);                          // give USB-CDC time to enumerate so we don't lose early prints

  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  pinMode(OFF_BUTTON_PIN,  INPUT_PULLUP);

  // ---- I2C bus (shared by gyro and matrix) ----
  Serial.println("Starting I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!mpu.begin()) {
    // No gyro? Wobble detection stays disabled forever (mpuOk = false).
    // The system is still useful — manual button + app trigger still work.
    Serial.printf("MPU6050 not found on SDA=%d SCL=%d — wobble detection DISABLED\n",
                  I2C_SDA, I2C_SCL);
    mpuOk = false;
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);     // ±500°/s = ±8.73 rad/s
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);  // low-pass filter cutoff
    mpuOk = true;
    Serial.println("MPU6050 ready");
  }

  if (matrix.begin(0x70)) {
    matrix.setBrightness(5);            // 0..15
    matrix.setRotation(1);              // 90° rotated for our mounting
    matrix.clear();
    matrix.writeDisplay();
    Serial.println("LED matrix ready");
  } else {
    Serial.println("LED matrix not found at 0x70 — display disabled");
  }

  // ---- ESP-NOW ----
  // ESP-NOW reuses the WiFi radio. We don't actually connect to a router,
  // so STA mode + no SSID is correct.
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init failed"); }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Add the slave as an unencrypted peer on whatever channel we're on.
  memcpy(peerInfo.peer_addr, slave1, 6);
  peerInfo.channel = 0;                 // 0 = "current channel"
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // ---- BLE ----
  // ESP32-C3 supports running BLE and ESP-NOW concurrently — they share
  // the radio at the controller level.
  BLEDevice::init("MC-Alarm");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *svc = server->createService(SVC_UUID);

  // Each NOTIFY characteristic needs a BLE2902 descriptor (the CCC) so the
  // phone can subscribe.
  chrAlarm = svc->createCharacteristic(CHAR_ALARM_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chrAlarm->addDescriptor(new BLE2902());

  chrWobble = svc->createCharacteristic(CHAR_WOBBLE_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chrWobble->addDescriptor(new BLE2902());
  // Seed with the boot value so a phone that READs (instead of waiting for
  // a NOTIFY) gets a known number, not undefined memory.
  chrWobble->setValue(&wobbleCounter, 1);

  chrCmd = svc->createCharacteristic(CHAR_CMD_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  chrCmd->setCallbacks(new CmdCallback());

  chrNodes = svc->createCharacteristic(CHAR_NODES_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chrNodes->addDescriptor(new BLE2902());

  svc->start();

  // Start advertising so the phone can find us.
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising as MC-Alarm");

  // Push initial values once so any phone that connects right now sees a
  // sensible state.
  publishAlarmState();
  publishNodeStatus(0);
}


// ----------------------------------------------------------------------------
// 16. MAIN LOOP — runs continuously
// ----------------------------------------------------------------------------
// The loop is intentionally non-blocking. Every section uses millis()-based
// scheduling so all the responsibilities (buttons, gyro, ESP-NOW, BLE,
// matrix) keep responding even while others are working.
void loop() {

  // ---- (a) physical buttons ----
  // Pressed = LOW (we use INPUT_PULLUP). The 250 ms delay after a press is
  // a poor man's debounce — it also limits how fast you can mash the button.
  if (digitalRead(TEST_BUTTON_PIN) == LOW) {
    setAlarm(true, "local button");
    delay(250);
  }
  if (digitalRead(OFF_BUTTON_PIN) == LOW) {
    // OFF also aborts a pending wobble before its 5-second grace expires.
    if (wobblePending) {
      wobblePending  = false;
      alarmStoppedAt = millis();
      Serial.println("Wobble cancelled by OFF button during grace period");
    }
    setAlarm(false, "local button");
    delay(250);
  }

  // ---- (b) wobble detection ----
  // Five conditions all have to be true for us to even look at the gyro:
  //   1. The MPU was actually found at boot (no garbage data).
  //   2. The alarm isn't already firing.
  //   3. We're not already in the pending grace period.
  //   4. We've waited the post-alarm cooldown.
  //   5. We've waited the startup grace.
  bool wobbleAllowed = mpuOk
                    && !alarmActive
                    && !wobblePending
                    && millis() > STARTUP_GRACE_MS
                    && (alarmStoppedAt == 0
                        || (millis() - alarmStoppedAt) > POST_ALARM_COOLDOWN);
  if (wobbleAllowed && checkForWobble()) {
    wobbleCounter++;
    publishWobble();                              // tell the phone NOW so the emergency screen pops with the countdown
    wobblePending      = true;
    wobblePendingUntil = millis() + WOBBLE_GRACE_MS;
    Serial.printf("Wobble detected — arming in %lu ms\n", WOBBLE_GRACE_MS);
  }

  // ---- (c) pending -> active transition ----
  // The cast to (long) makes the subtraction correctly handle millis() wraparound
  // (which only matters after ~50 days of uptime, but it's a free safety).
  if (wobblePending && (long)(millis() - wobblePendingUntil) >= 0) {
    wobblePending = false;
    setAlarm(true, "wobble (after grace)");
  }

  // ---- (d) heartbeat / node-status maintenance ----
  unsigned long now = millis();
  if (now - lastPingSent >= PING_INTERVAL) {
    lastPingSent = now;
    sendPing();                                   // ask each slave "are you there?"
  }
  // Build a fresh status bitmask from the per-slave timestamps.
  uint8_t mask = 0;
  for (int i = 0; i < NUM_SLAVES; ++i) {
    if (lastHeartbeat[i] != 0 && (now - lastHeartbeat[i]) < HEARTBEAT_TIMEOUT) {
      mask |= (1 << i);                           // slave i is online
    }
  }
  // Only publish if the mask actually changed — saves BLE traffic.
  if (mask != lastNodeStatus) {
    lastNodeStatus = mask;
    publishNodeStatus(mask);
    Serial.printf("Node status: 0x%02X\n", mask);
  }

  // ---- (e) hazard-triangle blink ----
  // While the alarm is active, toggle the matrix every blinkInterval ms.
  if (alarmActive) {
    if (now - lastBlinkTime >= (unsigned long)blinkInterval) {
      lastBlinkTime = now;
      matrixState   = !matrixState;
      if (matrixState) drawTriangle();
      else { matrix.clear(); matrix.writeDisplay(); }
    }
  }
}
