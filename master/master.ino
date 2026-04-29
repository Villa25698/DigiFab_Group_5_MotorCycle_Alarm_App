// ============================================================================
//  MC Alarm — Master (Gateway) firmware
//  Board: ESP32-C3 DevKitM-1
//
//  Roles of this node:
//    1. Read MPU-6050, detect "death-wobble" event.
//    2. Drive local siren + hazard-triangle on 8x8 LED matrix.
//    3. Talk to slave nodes over ESP-NOW  (hazard relay modules).
//    4. Talk to the phone over BLE GATT   (Flutter MC Alarm app).
//
//  BLE GATT layout (custom service):
//    Service  UUID  a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001
//      alarmState   a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0002  READ, NOTIFY   uint8
//      wobbleEvent  a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0003  NOTIFY         uint8 (counter)
//      command      a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0004  WRITE          uint8
//      nodeStatus   a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0005  READ, NOTIFY   uint8 bitmask
//
//  command values:
//    0 = stop alarm          1 = manual trigger       2 = toggle hazard-only
//
//  nodeStatus bitmask:
//    bit 0 = slave1 online   (more bits reserved if you add slaves later)
// ============================================================================

#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- 1. PIN CONFIGURATION (ESP32-C3 DevKit) -------------------------------
const int TEST_BUTTON_PIN = 4;
const int OFF_BUTTON_PIN  = 5;
const int speakerPin      = 7;
const int MPU_SDA         = 3;
const int MPU_SCL         = 2;
const int MATRIX_SDA      = 18;
const int MATRIX_SCL      = 19;

// --- 2. GLOBAL STATE ------------------------------------------------------
bool alarmActive = false;
uint8_t wobbleCounter = 0;            // incremented on each wobble detection (for BLE notify)

// 5-second arming grace period after a wobble is detected. During this
// window the rider can press the OFF button (local) or STOP (app) to abort
// before the siren / matrix / slave relay are fired.
bool wobblePending = false;
unsigned long wobblePendingUntil = 0;
const unsigned long WOBBLE_GRACE_MS = 5000;

// After an alarm is silenced (or a pending wobble cancelled), suppress new
// wobble detection for a short cooldown. The piezo siren physically shakes
// the gyro, and the bike itself may rock for a moment after the rider hits
// stop — without this cooldown the alarm immediately re-arms itself.
unsigned long alarmStoppedAt = 0;
const unsigned long POST_ALARM_COOLDOWN = 3000;

// Ignore wobble for a moment after boot so the gyro can settle.
const unsigned long STARTUP_GRACE_MS = 3000;

Adafruit_MPU6050 mpu;
Adafruit_8x8matrix matrix = Adafruit_8x8matrix();

unsigned long lastBlinkTime = 0;
const int    blinkInterval = 400;
bool matrixState = false;

// --- ESP-NOW message protocol --------------------------------------------
// msgType values
#define MSG_ALARM_STATE 0x01  // master -> slave : alarmActive
#define MSG_PING        0x02  // master -> slave : request heartbeat
#define MSG_HEARTBEAT   0x03  // slave  -> master: I'm alive + my nodeId

typedef struct __attribute__((packed)) struct_message {
    uint8_t msgType;
    bool    alarmActive;
    uint8_t nodeId;          // 0 in master-originated messages, slave's own id in heartbeats
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// Slave MACs
uint8_t slave1[] = {0xAC, 0xEB, 0xE6, 0x80, 0xF3, 0xE4};

// Heartbeat bookkeeping
#define NUM_SLAVES 1
unsigned long lastHeartbeat[NUM_SLAVES] = {0};
const unsigned long HEARTBEAT_TIMEOUT = 5000;   // ms without heartbeat => offline
const unsigned long PING_INTERVAL     = 1500;   // ms between pings
unsigned long lastPingSent = 0;
uint8_t lastNodeStatus = 0xFF;                  // force first publish

// --- BLE ------------------------------------------------------------------
#define SVC_UUID         "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001"
#define CHAR_ALARM_UUID  "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0002"
#define CHAR_WOBBLE_UUID "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0003"
#define CHAR_CMD_UUID    "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0004"
#define CHAR_NODES_UUID  "a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0005"

BLECharacteristic *chrAlarm  = nullptr;
BLECharacteristic *chrWobble = nullptr;
BLECharacteristic *chrCmd    = nullptr;
BLECharacteristic *chrNodes  = nullptr;
bool bleClientConnected = false;

// Forward declarations
void setAlarm(bool on, const char *reason);
void publishAlarmState();
void publishNodeStatus(uint8_t mask);

// --- 3. BLE CALLBACKS -----------------------------------------------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* srv) override {
    bleClientConnected = true;
    Serial.println("BLE: phone connected");
  }
  void onDisconnect(BLEServer* srv) override {
    bleClientConnected = false;
    Serial.println("BLE: phone disconnected, restarting advertising");
    BLEDevice::startAdvertising();
  }
};

class CmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (v.length() < 1) return;
    uint8_t cmd = (uint8_t)v[0];
    Serial.printf("BLE cmd received: %u\n", cmd);
    switch (cmd) {
      case 0:
        // Stop also aborts a pending wobble alarm during its grace period.
        if (wobblePending) {
          wobblePending  = false;
          alarmStoppedAt = millis();
          Serial.println("Wobble alarm cancelled by app during grace period");
        }
        setAlarm(false, "app stop");
        break;
      case 1: setAlarm(true,  "app manual");    break;
      case 2: setAlarm(!alarmActive, "app toggle"); break;
    }
  }
};

// --- 4. ESP-NOW CALLBACKS -------------------------------------------------
// Signatures match esp32 Arduino core 3.x. Older cores used (uint8_t*, ...).
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) Serial.println("ESP-NOW send failed");
}

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len != sizeof(struct_message)) return;
  struct_message incoming;
  memcpy(&incoming, data, sizeof(incoming));
  if (incoming.msgType == MSG_HEARTBEAT) {
    uint8_t id = incoming.nodeId;
    if (id >= 1 && id <= NUM_SLAVES) {
      lastHeartbeat[id - 1] = millis();
    }
  }
}

// --- 5. HELPERS -----------------------------------------------------------
void sendAlarmState(bool state) {
  myData.msgType     = MSG_ALARM_STATE;
  myData.alarmActive = state;
  myData.nodeId      = 0;
  esp_now_send(0, (uint8_t *)&myData, sizeof(myData));
}

void sendPing() {
  myData.msgType     = MSG_PING;
  myData.alarmActive = alarmActive;
  myData.nodeId      = 0;
  esp_now_send(0, (uint8_t *)&myData, sizeof(myData));
}

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

// Centralised alarm-state transition so BLE, ESP-NOW, siren and matrix stay in sync.
void setAlarm(bool on, const char *reason) {
  if (alarmActive == on) return;
  alarmActive = on;
  if (on) {
    tone(speakerPin, 1200);
    lastBlinkTime = millis();
  } else {
    noTone(speakerPin);
    digitalWrite(speakerPin, LOW);  // make absolutely sure the pin is silent
    Wire.begin(MATRIX_SDA, MATRIX_SCL);
    matrix.clear();
    matrix.writeDisplay();
    // Start the post-alarm cooldown so vibration from the speaker / bike
    // settling doesn't immediately re-trigger the wobble detector.
    alarmStoppedAt = millis();
  }
  sendAlarmState(on);
  publishAlarmState();
  Serial.printf("Alarm %s (%s)\n", on ? "ON" : "OFF", reason);
}

void drawTriangle() {
  matrix.clear();
  matrix.drawLine(3, 0, 4, 0, LED_ON);
  matrix.drawLine(3, 0, 0, 7, LED_ON);
  matrix.drawLine(4, 0, 7, 7, LED_ON);
  matrix.drawLine(0, 7, 7, 7, LED_ON);
  matrix.writeDisplay();
}

bool checkForWobble() {
  sensors_event_t a, g, temp;
  Wire.begin(MPU_SDA, MPU_SCL);
  mpu.getEvent(&a, &g, &temp);

  // Pick whichever gyro axis is moving the most this sample. That way the
  // board doesn't have to be mounted in a specific orientation for "shake to
  // test" to work — twisting around any axis triggers it.
  float gx = g.gyro.x, gy = g.gyro.y, gz = g.gyro.z;
  float ax = fabs(gx), ay = fabs(gy), az = fabs(gz);
  float currentRotation = gz;
  if (ax >= ay && ax >= az) currentRotation = gx;
  else if (ay >= ax && ay >= az) currentRotation = gy;

  const float THRESHOLD = 2.5;        // rad/s — minimum twist speed to count
                                      // a real handlebar wobble peaks 5–10 rad/s,
                                      // noise / table bumps stay under ~1 rad/s
  const int   FLIPS_REQUIRED = 4;     // sign reversals before we trip
  const unsigned long FLIP_WINDOW = 600; // ms; reset flip count if no flip in this long

  static float lastRotation = 0;
  static int   flipCount    = 0;
  static unsigned long lastFlipTime = 0;
  static unsigned long lastDebug    = 0;
  static unsigned long lastCallTime = 0;

  // If the detector wasn't being polled for a while (e.g. we just exited the
  // post-alarm cooldown) clear stale state so old flips don't count.
  unsigned long nowMs = millis();
  if (lastCallTime != 0 && nowMs - lastCallTime > 500) {
    lastRotation = 0;
    flipCount    = 0;
    lastFlipTime = nowMs;
  }
  lastCallTime = nowMs;

  // Both the current AND the previous sample must clear the threshold for a
  // sign change to count. A single big spike paired with noise no longer
  // produces a fake flip — only sustained back-and-forth twisting does.
  if (fabs(currentRotation) > THRESHOLD && fabs(lastRotation) > THRESHOLD) {
    if ((currentRotation > 0 && lastRotation < 0) || (currentRotation < 0 && lastRotation > 0)) {
      flipCount++;
      lastFlipTime = millis();
      Serial.printf("  flip %d  (rot=%.2f rad/s)\n", flipCount, currentRotation);
    }
  }
  if (millis() - lastFlipTime > FLIP_WINDOW) flipCount = 0;
  lastRotation = currentRotation;

  // Print live gyro values ~5x/sec so you can see what the sensor sees.
  if (millis() - lastDebug > 200) {
    lastDebug = millis();
    Serial.printf("gyro x=%.2f  y=%.2f  z=%.2f  pick=%.2f  flips=%d\n",
                  gx, gy, gz, currentRotation, flipCount);
  }

  return (flipCount >= FLIPS_REQUIRED);
}

// --- 6. SETUP -------------------------------------------------------------
void setup() {
  // Silence the speaker as the FIRST thing — before serial init, before any
  // delay. Until pinMode runs, GPIO 7 floats and the amp / piezo can sing.
  pinMode(speakerPin, OUTPUT);
  digitalWrite(speakerPin, LOW);
  noTone(speakerPin);

  Serial.begin(115200);
  delay(1000);

  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  pinMode(OFF_BUTTON_PIN,  INPUT_PULLUP);

  Wire.begin(MPU_SDA, MPU_SCL);
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  Wire.begin(MATRIX_SDA, MATRIX_SCL);
  if (matrix.begin(0x70)) {
    matrix.setBrightness(5);
    matrix.setRotation(1);
    matrix.clear();
    matrix.writeDisplay();
  }

  // ESP-NOW on STA interface
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init failed"); }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  memcpy(peerInfo.peer_addr, slave1, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // BLE — can coexist with ESP-NOW on C3
  BLEDevice::init("MC-Alarm");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *svc = server->createService(SVC_UUID);

  chrAlarm = svc->createCharacteristic(CHAR_ALARM_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chrAlarm->addDescriptor(new BLE2902());

  chrWobble = svc->createCharacteristic(CHAR_WOBBLE_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chrWobble->addDescriptor(new BLE2902());
  // Seed with the current counter so phones that read on connect don't see
  // an undefined / stale value left over in the BLE stack.
  chrWobble->setValue(&wobbleCounter, 1);

  chrCmd = svc->createCharacteristic(CHAR_CMD_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  chrCmd->setCallbacks(new CmdCallback());

  chrNodes = svc->createCharacteristic(CHAR_NODES_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chrNodes->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising as MC-Alarm");

  publishAlarmState();
  publishNodeStatus(0);
}

// --- 7. MAIN LOOP ---------------------------------------------------------
void loop() {
  // Manual ON
  if (digitalRead(TEST_BUTTON_PIN) == LOW) {
    setAlarm(true, "local button");
    delay(250);
  }
  // Manual OFF — also aborts a pending wobble alarm during its grace period.
  if (digitalRead(OFF_BUTTON_PIN) == LOW) {
    if (wobblePending) {
      wobblePending  = false;
      alarmStoppedAt = millis();
      Serial.println("Wobble alarm cancelled by OFF button during grace period");
    }
    setAlarm(false, "local button");
    delay(250);
  }

  // Wobble detection — gated on:
  //   1. alarm not currently firing
  //   2. no pending grace period already running
  //   3. we're past the post-alarm cooldown (gyro has settled)
  //   4. we're past the startup grace (gyro has finished warming up)
  bool wobbleAllowed = !alarmActive
                    && !wobblePending
                    && millis() > STARTUP_GRACE_MS
                    && (alarmStoppedAt == 0
                        || (millis() - alarmStoppedAt) > POST_ALARM_COOLDOWN);
  if (wobbleAllowed) {
    if (checkForWobble()) {
      wobbleCounter++;
      publishWobble();              // tell phone NOW so emergency screen pops
      wobblePending      = true;
      wobblePendingUntil = millis() + WOBBLE_GRACE_MS;
      Serial.printf("Wobble detected — arming in %lu ms\n", WOBBLE_GRACE_MS);
    }
  }

  // If grace period elapsed without cancellation, fire the alarm.
  if (wobblePending && (long)(millis() - wobblePendingUntil) >= 0) {
    wobblePending = false;
    setAlarm(true, "wobble (after grace)");
  }

  // Heartbeat / node-status maintenance
  unsigned long now = millis();
  if (now - lastPingSent >= PING_INTERVAL) {
    lastPingSent = now;
    sendPing();
  }
  uint8_t mask = 0;
  for (int i = 0; i < NUM_SLAVES; ++i) {
    if (lastHeartbeat[i] != 0 && (now - lastHeartbeat[i]) < HEARTBEAT_TIMEOUT) {
      mask |= (1 << i);
    }
  }
  if (mask != lastNodeStatus) {
    lastNodeStatus = mask;
    publishNodeStatus(mask);
    Serial.printf("Node status: 0x%02X\n", mask);
  }

  // LED matrix blink while alarm is active
  if (alarmActive) {
    if (now - lastBlinkTime >= (unsigned long)blinkInterval) {
      lastBlinkTime = now;
      matrixState = !matrixState;
      Wire.begin(MATRIX_SDA, MATRIX_SCL);
      if (matrixState) drawTriangle();
      else { matrix.clear(); matrix.writeDisplay(); }
    }
  }
}
