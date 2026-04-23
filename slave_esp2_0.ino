#include <esp_now.h>           // Library for ESP-NOW wireless communication
#include <WiFi.h>              // Required for ESP32 WiFi hardware (used by ESP-NOW)
#include <Adafruit_MPU6050.h>    // Library to communicate with the MPU6050 motion sensor
#include <Adafruit_Sensor.h>      // Helper library for unified sensor data
#include <Wire.h>                 // I2C communication library (the "language" the sensors speak)
#include <Adafruit_GFX.h>         // Graphics library for drawing shapes (lines, circles, etc.)
#include "Adafruit_LEDBackpack.h" // Specific driver for the 8x8 LED Matrix chip

// --- 1. PIN CONFIGURATION ---
const int TEST_BUTTON_PIN = 4; // GPIO 4: Manual "Emergency On" button
const int OFF_BUTTON_PIN = 5;  // GPIO 5: Manual "Emergency Off" button
const int speakerPin = 7;      // GPIO 7: Connected to your buzzer/speaker

// Define the two different I2C bus pin sets
const int MPU_SDA = 2;         // Data pin for the motion sensor
const int MPU_SCL = 3;         // Clock pin for the motion sensor
const int MATRIX_SDA = 18;     // Data pin for your LCD Matrix
const int MATRIX_SCL = 19;     // Clock pin for your LCD Matrix

// --- 2. GLOBAL OBJECTS & VARIABLES ---
Adafruit_MPU6050 mpu;             // Create an object representing the motion sensor
Adafruit_8x8matrix matrix = Adafruit_8x8matrix(); // Create an object for the LED Matrix

bool alarmActive = false;         // The master switch: is the hazard system ON or OFF?
unsigned long lastBlinkTime = 0;  // Stores the timestamp of the last LED blink
const int blinkInterval = 400;    // How long (ms) the blink stays ON or OFF (Matches slaves)
bool matrixState = false;         // Internal toggle to track if LEDs are currently ON or OFF

// MAC Addresses of the receiving ESP32s (Wireless Light Modules)
uint8_t slave1[] = {0xAC, 0xEB, 0xE6, 0x80, 0xF3, 0xE4};
uint8_t slave2[] = {0xAC, 0xEB, 0xE6, 0x80, 0x58, 0xC8};

// This structure defines the "packet" of data sent wirelessly
typedef struct struct_message {
    bool alarmActive;             // We only need to send a simple True/False to the slaves
} struct_message;

struct_message myData;            // Create an instance of our data packet
esp_now_peer_info_t peerInfo;    // Create a storage object for "Peers" (the slave ESP32s)

// --- 3. HELPER FUNCTIONS ---

// This function runs automatically whenever the ESP32 sends a wireless packet
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Empty, but used for debugging if you want to see if packets "Delivered" or "Failed"
}

// Sends the True/False alarm state to all registered slaves
void sendSignal(bool state) {
  myData.alarmActive = state;     // Put the state into our packet
  esp_now_send(0, (uint8_t *) &myData, sizeof(myData)); // Send to all (0 = Broadcast)
}

// Draws the hazard triangle on the 8x8 grid
void drawTriangle() {
  matrix.clear();                                // Clear previous drawing
  matrix.drawLine(3, 0, 4, 0, LED_ON);           // Draw top flat edge (2 pixels wide)
  matrix.drawLine(3, 0, 0, 7, LED_ON);           // Draw left diagonal side
  matrix.drawLine(4, 0, 7, 7, LED_ON);           // Draw right diagonal side
  matrix.drawLine(0, 7, 7, 7, LED_ON);           // Draw the bottom base line
  matrix.writeDisplay();                         // Push the drawing to the physical LEDs
}

// The "Detective" function: Analyzes gyro data to find rapid shaking
bool checkForWobble() {
  sensors_event_t a, g, temp;                    // Storage for acceleration/gyro/temp data
  Wire.begin(MPU_SDA, MPU_SCL);                  // Tell ESP32 to talk to the MPU pins
  mpu.getEvent(&a, &g, &temp);                   // Ask the sensor for current movement

  float currentRotation = g.gyro.z;              // Focus on Yaw (steering rotation)
  static float lastRotation = 0;                 // Remembers previous rotation for comparison
  static int flipCount = 0;                      // Counts how many times steering "flipped" direction
  static unsigned long lastFlipTime = 0;         // Timestamp of the last direction change

  // If rotation is faster than 1.5 rad/s (violent movement)
  if (abs(currentRotation) > 1.5) {
    // Check if the direction changed (e.g., went from rotating Right to rotating Left)
    if ((currentRotation > 0 && lastRotation < 0) || (currentRotation < 0 && lastRotation > 0)) {
      flipCount++;                               // We found a direction flip!
      lastFlipTime = millis();                   // Update the timestamp
    }
  }

  // If 400ms passes without a flip, assume the wobble stopped and reset counter
  if (millis() - lastFlipTime > 400) flipCount = 0; 
  
  lastRotation = currentRotation;                // Update "last" for the next loop
  return (flipCount >= 4);                       // If 4 flips happened quickly, return TRUE (Danger!)
}

// --- 4. MAIN SETUP (Runs Once) ---
void setup() {
  Serial.begin(115200);                         // Open Serial Monitor for debugging messages

  // Configure button pins with pullup resistors (Resting = HIGH, Pressed = LOW)
  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  pinMode(OFF_BUTTON_PIN, INPUT_PULLUP);
  pinMode(speakerPin, OUTPUT);                   // Set buzzer pin as an output

  // Initialize the MPU6050 Sensor
  Wire.begin(MPU_SDA, MPU_SCL);                  // Initialize the I2C bus for the MPU
  if (!mpu.begin()) {
    Serial.println("MPU6050 connection failed!");
  }

  // Initialize the LED Matrix
  Wire.begin(MATRIX_SDA, MATRIX_SCL);            // Re-initialize I2C bus for Matrix pins
  if (matrix.begin(0x70)) {                      // 0x70 is the default address of the matrix
    matrix.setBrightness(5);                     // Set brightness (0-15)
    matrix.setRotation(1);                       // Rotate drawing if needed
    matrix.clear();                              // Start with a blank screen
    matrix.writeDisplay();
  }

  // Set up Wireless Communication (ESP-NOW)
  WiFi.mode(WIFI_STA);                           // Put WiFi in Station mode
  esp_now_init();                                // Start the ESP-NOW protocol
  esp_now_register_send_cb(esp_now_send_cb_t(OnDataSent)); // Link the "Sent" function
  
  // Register Slave 1
  memcpy(peerInfo.peer_addr, slave1, 6);
  peerInfo.channel = 0; 
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  
  // Register Slave 2
  memcpy(peerInfo.peer_addr, slave2, 6);
  esp_now_add_peer(&peerInfo);
}

// --- 5. MAIN LOOP (Runs Millions of times per second) ---
void loop() {
  // 1. Check Manual ON Button
  if (digitalRead(TEST_BUTTON_PIN) == LOW && !alarmActive) {
    alarmActive = true;                          // Switch master state to ON
    tone(speakerPin, 1000);                      // Start the siren
    sendSignal(true);                            // Tell Slaves to turn on
    lastBlinkTime = millis();                    // Sync the first blink
    delay(250);                                  // Small delay to prevent "double-pressing"
  }

  // 2. Check Manual OFF Button
  if (digitalRead(OFF_BUTTON_PIN) == LOW && alarmActive) {
    alarmActive = false;                         // Switch master state to OFF
    noTone(speakerPin);                          // Silence the siren
    sendSignal(false);                           // Tell Slaves to turn off
    
    Wire.begin(MATRIX_SDA, MATRIX_SCL);          // Switch to Matrix pins
    matrix.clear();                              // Turn off all LEDs
    matrix.writeDisplay();
    delay(250);                                  // Small delay to prevent "double-pressing"
  }

  // 3. Check Automatic Sensor (Wobble Detection)
  // Only check for wobbles if the alarm isn't already running
  if (!alarmActive && checkForWobble()) {
    alarmActive = true;                          // Danger detected! Trigger system
    tone(speakerPin, 1300);                      // Use a higher pitch siren for auto-trigger
    sendSignal(true);                            // Sync Slaves
    lastBlinkTime = millis();                    // Sync Matrix blink
  }

  // 4. ANIMATION (Non-Blocking Blink)
  // This logic runs only if the alarm is active
  if (alarmActive) {
    unsigned long currentMillis = millis();      // Get the current time
    
    // If enough time (400ms) has passed since the last change
    if (currentMillis - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = currentMillis;             // Save the new "last" time
      matrixState = !matrixState;                // Toggle the state (ON -> OFF or OFF -> ON)

      Wire.begin(MATRIX_SDA, MATRIX_SCL);        // Ensure we are talking to Matrix pins
      if (matrixState) {
        drawTriangle();                          // If state is True, draw it
      } else {
        matrix.clear();                          // If state is False, hide it
        matrix.writeDisplay();
      }
    }
  }
}