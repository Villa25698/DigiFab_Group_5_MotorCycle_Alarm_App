#include <esp_now.h>        // Wireless communication library
#include <WiFi.h>           // WiFi library (required for ESP-NOW)
#include <Adafruit_MPU6050.h> // Motion sensor library
#include <Adafruit_Sensor.h>   // Base sensor library
#include <Wire.h>              // I2C library (for sensor communication)
#include <Adafruit_GFX.h>         // Graphics library for drawing shapes (lines, circles, etc.) on the LED matrix
#include "Adafruit_LEDBackpack.h" // Driver library for the HT16K33-based 8x8 LED matrix backpack

// --- 1. PIN CONFIGURATION (ESP32-C3 DevKit) ---
const int TEST_BUTTON_PIN = 4; // Manual ON button
const int OFF_BUTTON_PIN = 5;  // Manual OFF button
const int speakerPin = 7;      // Alarm speaker/buzzer
const int I2C_SDA = 2;         // Shared I2C data line (MPU-6050 + LED matrix)
const int I2C_SCL = 3;         // Shared I2C clock line (MPU-6050 + LED matrix)


// --- 2. GLOBAL VARIABLES ---
bool alarmActive = false;      // Current state of the alarm system
Adafruit_MPU6050 mpu;          // Sensor object
Adafruit_8x8matrix matrix = Adafruit_8x8matrix(); // Create an object to control the 8x8 LED matrix display

// --- Blink timing variables for the LED matrix ---
// These control the non-blocking blink so the triangle flashes ON and OFF
unsigned long lastBlinkTime = 0;  // Stores the timestamp (in ms) of the last blink toggle
const int blinkInterval = 400;    // Time in ms between each ON/OFF toggle (matches the slave ESP blink speed)
bool matrixState = false;         // Tracks whether the matrix is currently showing (true) or blank (false)

// MAC Addresses of your slaves (Wireless Light Modules)
uint8_t slave1[] = {0xAC, 0xEB, 0xE6, 0x80, 0xF3, 0xE4}; // This is for the blink lights 
uint8_t slave2[] = {0xAC, 0xEB, 0xE6, 0x80, 0x58, 0xC8}; // If we have another blink module will this be used

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

// Draws a hazard warning triangle on the 8x8 LED matrix
// The triangle is drawn with a 2-pixel-wide top, diagonal sides, and a full bottom base
void drawTriangle() {
  matrix.clear();                                // Clear any previous drawing from the display buffer
  matrix.drawLine(3, 0, 4, 0, LED_ON);           // Draw top flat edge (2 pixels wide at row 0)
  matrix.drawLine(3, 0, 0, 7, LED_ON);           // Draw left diagonal side from top-center to bottom-left
  matrix.drawLine(4, 0, 7, 7, LED_ON);           // Draw right diagonal side from top-center to bottom-right
  matrix.drawLine(0, 7, 7, 7, LED_ON);           // Draw the bottom base line across the full width
  matrix.writeDisplay();                         // Push the completed drawing from buffer to the physical LEDs
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
  if (abs(currentRotation) > 0.5) {
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

  // Start shared I2C bus (MPU6050 + LED matrix live on the same bus, different addresses)
  Serial.println("Starting I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);

  // Start Motion Sensor
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found! Check wiring on pins SDA:" + String(I2C_SDA) + " SCL:" + String(I2C_SCL));
    // Don't just continue; if it's not found, the loop will crash later
  } else {
    Serial.println("MPU6050 Ready!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  // Initialize the 8x8 LED Matrix on the shared I2C bus (address 0x70)
  if (!matrix.begin(0x70)) {
    Serial.println("Matrix not found at 0x70! Check wiring.");
  } else {
    matrix.setBrightness(5);                     // Set LED brightness level (range 0-15, 5 is moderate)
    matrix.setRotation(1);                       // Rotate the display orientation by 90 degrees if needed to match physical mounting
    matrix.clear();                              // Clear the display buffer so no random LEDs are lit on startup
    matrix.writeDisplay();                       // Push the cleared buffer to the matrix so it starts blank
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
        lastBlinkTime = millis();                  // Sync the matrix blink timer to start immediately
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

        // Turn off the LED matrix when the alarm is deactivated
        matrix.clear();                            // Clear the display buffer (all LEDs off)
        matrix.writeDisplay();                     // Push the blank buffer to physically turn off the LEDs

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
      lastBlinkTime = millis();                    // Sync the matrix blink timer when wobble triggers alarm
      Serial.println("WOBBLE DETECTED - ALARM ACTIVE");
    }
  }

  // --- LED Matrix Blinking Logic (Non-Blocking) ---
  // This section only runs while the alarm is active.
  // It uses millis() instead of delay() so it doesn't block the rest of the loop
  // (buttons and wobble detection keep working during the blink animation).
  // The blink interval (400ms) matches the slave ESP blink speed so they flash in sync.
  if (alarmActive) {
    unsigned long currentMillis = millis();        // Capture the current time once for consistency

    // Check if enough time has passed since the last blink toggle
    if (currentMillis - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = currentMillis;               // Update the last blink timestamp to "now"
      matrixState = !matrixState;                  // Flip the toggle: if it was ON, make it OFF, and vice versa

      if (matrixState) {
        drawTriangle();                            // ON phase: draw the hazard triangle on the matrix
      } else {
        matrix.clear();                            // OFF phase: clear the display buffer
        matrix.writeDisplay();                     // Push the blank buffer to turn off all LEDs
      }
    }
  }
}