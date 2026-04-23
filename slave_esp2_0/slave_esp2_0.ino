#include <esp_now.h>
#include <WiFi.h>

const int relayPin = 18; // Which pin we are using to send signal to the relay
bool shouldBlink = false; // Using this to test the light 

// Structure to receive data (Must match Master's structure)
typedef struct struct_message {
    bool alarmActive;
} struct_message;

struct_message myData;

// Callback function executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  shouldBlink = myData.alarmActive;
}

void setup() {
  Serial.begin(115200);
  
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // Assuming Active Low relay (Off)

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register the receive callback
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void loop() {
  // Here should the code for reciving the signal from the master esp with esp now
  //shouldBlink = true; // True to test if it will start blinking
  if (shouldBlink) {
    digitalWrite(relayPin, LOW);  // Turn ON
    delay(750);                   // Speed of the blinks
    digitalWrite(relayPin, HIGH); // Turn OFF
    delay(750);
  } else {
      digitalWrite(relayPin, LOW); // To make it turn off if shouldBlink is false

  }

}