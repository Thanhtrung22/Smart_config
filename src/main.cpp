#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Arduino.h>
#include <PubSubClient.h>

/* UUID for BLE */
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
/* MQTT variables */
const char* mqtt_server = "192.168.179.157";
const int mqtt_port = 1883;
const char* mqtt_topic = "thanhtrung_smartconfig";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
String ssid = "";
String password = "";

const byte xorKey[] = {0x01, 0x02, 0x03}; // XOR key for encryption

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

void setup() {
  Serial.begin(115200);
  BLEDevice::init("SmartConfig");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

void encryptValue(String& value) {
  for (int i = 0; i < value.length(); i++) {
    value[i] = value[i] ^ xorKey[i % sizeof(xorKey)];
  }
}

void decryptValue(String& value) {
  for (int i = 0; i < value.length(); i++) {
    value[i] = value[i] ^ xorKey[i % sizeof(xorKey)];
  }
}

byte calculateChecksum(const String& data) {
  byte checksum = 0;
  for (int i = 0; i < data.length(); i++) {
    checksum ^= data[i];
  }
  return checksum;
}
bool verifyChecksum(const String& data, byte checksum) {
  byte calculatedChecksum = calculateChecksum(data);
  return calculatedChecksum == checksum;
}

void setupWiFiConnection() {
  unsigned long startTime = millis();
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime >= 15000) {
      Serial.println("");
      Serial.println("Connection failed, please check your Wi-Fi information and try again.");
      Serial.println("Enter the Wi-Fi information again in the format 'SSID,PASSWORD'");
      while (WiFi.status() != WL_CONNECTED) {
        // Wait for new Wi-Fi information to be entered
        String newValue = pCharacteristic->getValue().c_str();
        encryptValue(newValue);
        if (newValue.length() > 0) {
          byte checksum =  calculateChecksum(newValue);
        // Verify the checksum
        if (verifyChecksum(newValue, checksum)){
          Serial.println("Checksum verification successful");
          decryptValue(newValue);
          int commaIndex = newValue.indexOf(',');
          if (commaIndex != -1) {
            ssid = newValue.substring(0, commaIndex);
            password = newValue.substring(commaIndex + 1);
            Serial.println("Wi-Fi information entered, connecting...");
            startTime = millis();
            WiFi.begin(ssid.c_str(), password.c_str());
            break;
          }
        }else {
            Serial.println("Checksum verification failed. Please enter the Wi-Fi information again.");
            break;
          }
        }
      }
    }
    delay(1000);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("Wi-Fi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void setupMQTTConnection() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT server...");
    String clientId = "ThanhTrungESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Connected to MQTT broker");
      mqttClient.subscribe(mqtt_topic);
      Serial.println("Subscribed to topic: " + String(mqtt_topic));
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void publishSuccessMessage() {
  String successMessage = "SmartConfig successful. Device connected to Wi-Fi network with IP address " + WiFi.localIP().toString();
  mqttClient.publish(mqtt_topic, successMessage.c_str());
  Serial.println("Success message sent to MQTT server: " + successMessage);
}



void loop()
{

  unsigned long lastBLEConnectionTime = 0;
  if (deviceConnected) {
    if (!oldDeviceConnected) {
      Serial.println("Device connected");
      oldDeviceConnected = true;
    }
    String value = pCharacteristic->getValue().c_str();
    encryptValue(value);
    if (value.length() > 0) {
      Serial.println("Received value: " + value);

       byte checksum =  calculateChecksum(value);
        // Verify the checksum
        if (verifyChecksum(value, checksum)) {
          Serial.println("Checksum verification successful");
          String decryptedValue = value;
          decryptValue(decryptedValue);

          int commaIndex = decryptedValue.indexOf(',');
          if (commaIndex != -1) {
            ssid = decryptedValue.substring(0, commaIndex);
            password = decryptedValue.substring(commaIndex + 1);

            setupWiFiConnection();

            mqttClient.setServer(mqtt_server, mqtt_port);
            setupMQTTConnection();
            publishSuccessMessage();
          }
        } else {
          Serial.println("Checksum verification failed");
        }
      }
      lastBLEConnectionTime = millis();
    }
    
    else {
    if (oldDeviceConnected) {
      Serial.println("Device disconnected");
      oldDeviceConnected = false;
    }
  }

  if (millis() - lastBLEConnectionTime > 5 * 60 * 1000 && mqttClient.connected()) {
    Serial.println("Disconnecting MQTT...");
    mqttClient.disconnect();
  }
}
