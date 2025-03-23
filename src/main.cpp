#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// Fonksiyon bildirimi
void connectToMQTT();

// -- MFRC522 Ayarları --
#define RST_PIN 22    // Reset pin
#define SS_PIN  5     // SPI chip select
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;  // Authentication key

// -- Wi-Fi Ayarları --
const char* ssid     = "Mert";        // Wi-Fi ağ adı
const char* password = "12345678";    // Wi-Fi şifresi

// -- RabbitMQ (MQTT) Sunucu Ayarları --
const int   mqttPort     = 1883;      // MQTT portu
// Dinamik olarak WiFi.gatewayIP() kullanılacak
const char* mqttUser     = "mert";    // RabbitMQ kullanıcı adı
const char* mqttPassword = "1234";    // RabbitMQ şifresi

// MQTT istemcisi için global nesneler
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Zamanlama değişkenleri
unsigned long lastHeartbeatTime = 0;
const unsigned long heartbeatInterval = 600000;  // 10 dakika (600.000 ms)

unsigned long lastDataPublishTime = 0;
const unsigned long dataPublishCooldown = 5000;    // Aynı kart verisi için en az 5 saniye

String lastData = "";  // Son okunan blok 4 verisi

void setup() {
  Serial.begin(9600);

  // MFRC522 başlatma
  SPI.begin();
  mfrc522.PCD_Init();
  delay(4);
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println(F("Scan an RFID card to read data from Block 4..."));

  // Default key for authentication (Factory default: 0xFFFFFFFFFFFF)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  // Wi-Fi bağlantısı
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway IP: ");
  Serial.println(WiFi.gatewayIP());

  // MQTT ayarları: Keepalive süresini 30 saniyeye çıkarıyoruz
  mqttClient.setKeepAlive(30);
  mqttClient.setServer(WiFi.gatewayIP(), mqttPort);
  
  // MQTT bağlantısı
  connectToMQTT();

  // Heartbeat zamanlayıcısını başlat
  lastHeartbeatTime = millis();
}

void loop() {
  // MQTT bağlantısını kontrol et
  if (!mqttClient.connected()) {
    connectToMQTT();
  }
  mqttClient.loop();

  unsigned long currentMillis = millis();

  // Her 10 dakikada bir heartbeat mesajı gönder (sistem sağlıklı çalışıyor)
  if (currentMillis - lastHeartbeatTime >= heartbeatInterval) {
    const char* heartbeatMessage = "System OK";
    mqttClient.publish("system/heartbeat", heartbeatMessage);
    Serial.print("Heartbeat sent: ");
    Serial.println(heartbeatMessage);
    lastHeartbeatTime = currentMillis;
  }

  // RFID kart okuma işlemi
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  // 4. bloktan veri okumak için
  byte block = 4;  
  byte buffer[18];
  byte bufferSize = sizeof(buffer);

  // Key A ile doğrulama
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }
  
   // Blok verisini oku
 status = mfrc522.MIFARE_Read(block, buffer, &bufferSize);
 if (status != MFRC522::STATUS_OK) {
   Serial.print(F("Read failed: "));
   Serial.println(mfrc522.GetStatusCodeName(status));
 } else {
   // Okunan 16 baytı ASCII string olarak birleştiriyoruz
   String dataString = "";
   for (byte i = 0; i < 16; i++) {
     dataString += (char) buffer[i];
   }

  // Aynı veriyi tekrardan göndermemek için ya da belirli süre geçtiyse yayınla
  if (dataString != lastData || (currentMillis - lastDataPublishTime >= dataPublishCooldown)) {
    mqttClient.publish("test_queue", dataString.c_str());
    Serial.print("Published Data (ASCII): ");
    Serial.println(dataString);
    lastData = dataString;
    lastDataPublishTime = currentMillis;
  }

    // Aynı veriyi tekrardan göndermemek için ya da belirli süre geçtiyse yayınla
    if (dataString != lastData || (currentMillis - lastDataPublishTime >= dataPublishCooldown)) {
      mqttClient.publish("test_queue", dataString.c_str());
      Serial.print("Published Data: ");
      Serial.println(dataString);
      lastData = dataString;
      lastDataPublishTime = currentMillis;
    }
  }

  // Kartı durdur ve şifrelemeyi kapat
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void connectToMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting to RabbitMQ (MQTT)...");
    // Benzersiz Client ID oluşturmak için rastgele sayı ekleyelim
    String clientId = "ESP32ClientID-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword)) {
      Serial.print("MQTT connected! Client ID: ");
      Serial.println(clientId);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retry in 2 seconds");
      delay(2000);
    }
  }
}
