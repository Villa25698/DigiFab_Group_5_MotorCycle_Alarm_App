#include <esp_now.h>        // Wireless communication library
#include <WiFi.h>           // WiFi library (required for ESP-NOW)
#include <Adafruit_MPU6050.h> // Motion sensor library
#include <Adafruit_Sensor.h>   // Base sensor library
#include <Wire.h>              // I2C library (for sensor communication)

// --- 1. PIN CONFIGURATION (ESP32-C3 DevKit) ---
const int TEST_BUTTON_PIN = 4; // Manual ON button
const int OFF_BUTTON_PIN = 5;  // Manual OFF button
const int speakerPin = 7;      // Alarm speaker/buzzer
const int MPU_SDA = 2;         // Data line for MPU-6050
const int MPU_SCL = 3;         // Clock line for MPU-6050


// --- 2. GLOBAL VARIABLES ---
bool alarmActive = false;      // Current state of the alarm system
Adafruit_MPU6050 mpu;          // Sensor object

// MAC Addresses of your slaves (Wireless Light Modules)
uint8_t slave1[] = {0xAC, 0xEB, 0xE6, 0x80, 0xF3, 0xE4};
uint8_t slave2[] = {0xAC, 0xEB, 0xE6, 0x80, 0x58, 0xC8};

// The "Message Envelope" structure
typedef struct struct_message {
    bool alarmActive; 
} struct_message;

struct_message myData;        // Instance of the message
esp_now_peer_info_t peerInfo; // Slave registration info

// --- 3. HELPER FUNCTIONS ---

// Function runs when data is sent wirelessly
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nPacket Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered" : "Failed");
}

// Function to send the ON/OFF signal to all slaves
void sendSignal(bool state) {
  myData.alarmActive = state;
  esp_now_send(0, (uint8_t *) &myData, sizeof(myData)); // 0 = send to all peers
}

// The "Wobble Detective" - Checks for rapid steering shakes
bool checkForWobble() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp); // Get current motion data

  Serial.print("Accel_X:"); Serial.print(a.acceleration.x); Serial.print(",");
  Serial.print("Gyro_Z:"); Serial.println(g.gyro.z);

  float currentRotation = g.gyro.z; // Focus on Yaw (steering rotation)
  static float lastRotation = 0;
  static int flipCount = 0;
  static unsigned long lastFlipTime = 0;

  // Threshold: If rotation speed is violent (> 3.5 rad/s)
  if (abs(currentRotation) > 1.5) {
    // Check for direction change (Right-to-Left or Left-to-Right)
    if ((currentRotation > 0 && lastRotation < 0) || (currentRotation < 0 && lastRotation > 0)) {
      flipCount++;
      lastFlipTime = millis();
    }
  }

  // If no shaking is detected for 400ms, reset the counter
  if (millis() - lastFlipTime > 400) {
    flipCount = 0; 
  }

  lastRotation = currentRotation;

  // If we count 4 rapid flips, a death wobble is happening
  return (flipCount >= 4);
}

// --- 4. MAIN SETUP (Runs Once) ---
void setup() {
  Serial.begin(115200);
  delay(1000); // Give the MPU6050 time to stabilize after power-on

  // Initialize Pins
  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  pinMode(OFF_BUTTON_PIN, INPUT_PULLUP);
  pinMode(speakerPin, OUTPUT);

  // Start I2C for ESP32-C3
  Serial.println("Starting I2C...");
  Wire.begin(MPU_SDA, MPU_SCL);

  // Start Motion Sensor
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found! Check wiring on pins SDA:" + String(MPU_SDA) + " SCL:" + String(MPU_SCL));
    // Don't just continue; if it's not found, the loop will crash later
  } else {
    Serial.println("MPU6050 Ready!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); 
  }

  // Start Wireless ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_send_cb(esp_now_send_cb_t(OnDataSent));
  
  // Register Slaves
  memcpy(peerInfo.peer_addr, slave1, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  memcpy(peerInfo.peer_addr, slave2, 6);
  esp_now_add_peer(&peerInfo);
}

// --- 5. MAIN LOOP (Runs Continuously) ---
void loop() {
  // Check for Manual ON button
  if (digitalRead(TEST_BUTTON_PIN) == LOW) {
    if (!alarmActive) {
        alarmActive = true;
        tone(speakerPin, 1000); // Siren sound
        sendSignal(true);
        Serial.println("Alarm Activated Manually");
    }
    delay(250); 
  }

  // Check for Manual OFF button
  if (digitalRead(OFF_BUTTON_PIN) == LOW) {
    if (alarmActive) {
        alarmActive = false;
        noTone(speakerPin); // Silence sound
        sendSignal(false);
        Serial.println("Alarm Deactivated");
    }
    delay(250); 
  }

  // Check for Automatic Wobble Detection
  if (!alarmActive) {
    if (checkForWobble()) {
      alarmActive = true;
      tone(speakerPin, 1300); // Higher pitch siren
      sendSignal(true);
      Serial.println("WOBBLE DETECTED - ALARM ACTIVE");
    }
  }
}