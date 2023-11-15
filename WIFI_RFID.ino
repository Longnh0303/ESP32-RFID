#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define SS_PIN 5
#define RST_PIN 4
#define LED_G 26 // Green LED pin
#define LED_R 25 // Red LED pin
#define BUZZER 27 // Buzzer pin

const char *ssid = "PhuongLinh_T1";
const char *password = "phuonglinh99";
const char *mqtt_server = "192.168.33.103";
const char *mqtt_topic = "status";
const int mqtt_port = 1883;  // Default MQTT port
const char *mqtt_username = "longnh";
const char *mqtt_password = "1";
bool doorStatus = false; // false means the door is closed

WiFiClient espClient;
PubSubClient client(espClient);

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance.

String removeSpaces(String input) {
  String result = "";
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] != ' ') {
      result += input[i];
    }
  }
  return result;
}

unsigned long lastMQTTMessageTime = 0;
const unsigned long mqttMessageInterval = 1000; // 1 second
const unsigned long cardReadInterval = 3000; // 3 seconds
unsigned long lastCardReadTime = 0;
bool canReadCard = true;

void setup() {
  Serial.begin(115200); // Initiate a serial communication

  SPI.begin();           // Initiate SPI bus
  mfrc522.PCD_Init();    // Initiate MFRC522

  delay(1000);
  // Connect to WiFi
  connectToWiFi();

  // Initialize MQTT
  client.setServer(mqtt_server, mqtt_port);
  connectToMQTT();

  pinMode(LED_G, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  noTone(BUZZER);

  Serial.println("Put your card to the reader...");
  Serial.println();
}

unsigned long doorOpenTime = 0;
const unsigned long doorOpenDuration = 5000;

void loop() {

  // Reconnect to Wi-Fi if not connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection lost. Reconnecting...");
    connectToWiFi();
  }

  // Reconnect to MQTT if not connected
  if (!client.connected()) {
    Serial.println("MQTT connection lost. Reconnecting...");
    connectToMQTT();
  }

  // Handle MQTT connection in the main loop
  client.loop();

  // Send door status to MQTT if one second has passed since the last message
  if (millis() - lastMQTTMessageTime >= mqttMessageInterval) {
    sendDoorStatusToMQTT();
    lastMQTTMessageTime = millis(); // Record the time the message was sent
  }

  // Check if enough time has passed to read another card
  if (canReadCard && millis() - lastCardReadTime >= cardReadInterval) {
    // Look for new cards
    if (mfrc522.PICC_IsNewCardPresent()) {
      // Select one of the cards
      if (mfrc522.PICC_ReadCardSerial()) {
        // Show UID on the serial monitor
        Serial.print("UID tag :");
        String content = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
          Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
          Serial.print(mfrc522.uid.uidByte[i], HEX);
          content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
          content.concat(String(mfrc522.uid.uidByte[i], HEX));
        }
        Serial.println();
        Serial.print("Message : ");
        content.toUpperCase();

        // Send HTTP request with card UID
        int httpCode = sendHTTPRequest(content.substring(1));

        if (httpCode == HTTP_CODE_OK) {
          Serial.println("Authorized access");
          Serial.println();
          doorStatus = true;
          digitalWrite(LED_G, HIGH);
          tone(BUZZER, 2000);
          delay(500);
          noTone(BUZZER);
          digitalWrite(LED_G, LOW);
          doorOpenTime = millis(); // Record the time the door was opened
        } else {
          Serial.println("Access denied");
          Serial.println();
          delay(500);
          digitalWrite(LED_R, HIGH);
          tone(BUZZER, 100);
          delay(1000);
          digitalWrite(LED_R, LOW);
          noTone(BUZZER);
        }

        canReadCard = false; // Disable reading card for the next 3 seconds
        lastCardReadTime = millis(); // Record the time the card was read
      }
    }
  }

  // Check if the door has been open for more than the specified duration
  if (doorStatus == true && (millis() - doorOpenTime) >= doorOpenDuration) {
    doorStatus = false;
  }

  // Enable reading card after 3 seconds
  if (!canReadCard && millis() - lastCardReadTime >= cardReadInterval) {
    canReadCard = true;
  }
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA); //Optional
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTT() {
  while (!client.connected()) {
    if (client.connect("ESP32Client", mqtt_username, mqtt_password)) {
      Serial.println("Connected to MQTT broker");
    } else {
      Serial.println("Failed to connect to MQTT broker, retrying in 5 seconds...");
      delay(5000);
    }
  }
}

int sendHTTPRequest(String cardUID) {
  HTTPClient http;
  cardUID = removeSpaces(cardUID);

  // Get the MAC address of the ESP
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String macAddress = "";
  for (int i = 0; i < 6; ++i) {
    macAddress += String(mac[i], HEX);
    if (i < 5) {
      macAddress += ":";
    }
  }

  // Construct the URL with the card UID as a parameter
  String url = "http://192.168.33.103:5000/api/gate";

  // Include both card ID and MAC address in the request body
  String requestBody = "{\"cardId\":\"" + cardUID + "\",\"macAddress\":\"" + macAddress + "\"}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(requestBody);

  http.end();
  return httpCode;
}


void sendDoorStatusToMQTT() {
  // Get the MAC address of the ESP
  uint8_t mac[6];
  WiFi.macAddress(mac);
  
  // Construct the MQTT payload with door status and MAC address
  String mqttPayload = "{\"status\":\"" + String(doorStatus) + "\",\"mac\":\"";
  for (int i = 0; i < 6; ++i) {
    mqttPayload += String(mac[i], HEX);
    if (i < 5) {
      mqttPayload += ":";
    }
  }
  mqttPayload += "\"}";
  
  // Publish the MQTT payload to the specified topic
  client.publish(mqtt_topic, mqttPayload.c_str(), true);
}
