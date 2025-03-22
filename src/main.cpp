#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// MFRC522 ayarları
#define RST_PIN 22        // Reset pini
#define SS_PIN 5          // Slave Select pini
MFRC522 mfrc522(SS_PIN, RST_PIN);  // MFRC522 örneği

// Fonksiyon bildirimi
void connectToMQTT();

// -- Wi-Fi Ayarları --
const char* ssid     = "Mert";        // Wi-Fi ağ adı
const char* password = "12345678";    // Wi-Fi şifresi

// -- RabbitMQ (MQTT) Sunucu Ayarları --
// Dinamik olarak WiFi.gatewayIP() kullanılacak
const int   mqttPort     = 1883;      // MQTT portu
const char* mqttUser     = "mert";    // RabbitMQ kullanıcı adı
const char* mqttPassword = "1234";    // RabbitMQ şifresi

// MQTT istemcisi için global nesneler
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Zamanlama değişkenleri
unsigned long lastHeartbeatTime = 0;
const unsigned long heartbeatInterval = 600000;  // 10 dakika (600.000 ms)

unsigned long lastCardPublishTime = 0;
const unsigned long cardPublishCooldown = 5000;    // Aynı kart için en az 5 saniye arayla publish

String lastUID = "";

void setup() {
  Serial.begin(9600);

  // MFRC522 başlatma
  SPI.begin();
  mfrc522.PCD_Init();
  delay(4); // Bazı kartlar için küçük gecikme gerekebilir
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println(F("RFID reader initialized. Scan a card..."));

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

  // MQTT ayarları: keepalive süresini 30 saniyeye çıkarıyoruz
  mqttClient.setKeepAlive(30);
  // WiFi.gatewayIP() dinamik olarak bağlı olduğunuz ağın geçit IP'sini verir
  mqttClient.setServer(WiFi.gatewayIP(), mqttPort);

  // MQTT'ya bağlan
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

  // Her 10 dakikada bir heartbeat mesajı gönder ("System OK")
  if (currentMillis - lastHeartbeatTime >= heartbeatInterval) {
    const char* heartbeatMessage = "System OK";
    mqttClient.publish("system/heartbeat", heartbeatMessage);
    Serial.print("Heartbeat sent: ");
    Serial.println(heartbeatMessage);
    lastHeartbeatTime = currentMillis;
  }

  // RFID kart okuma işlemi
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return; // Yeni kart yoksa döngüyü erken terk et
  }
  if (!mfrc522.PICC_ReadCardSerial()) {
    return; // Kart okunamadıysa döngüyü terk et
  }

  // UID bilgisini oluştur
  String uidString = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uidString += "0";
    }
    uidString += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidString.toUpperCase();

  // Eğer yeni bir kart okunmuşsa ya da son yayınlanan UID’den belirli bir süre geçtiyse mesaj gönder
  if (uidString != lastUID || (currentMillis - lastCardPublishTime >= cardPublishCooldown)) {
    // UID bilgisini "rfid/uid" topic'ine gönder
    mqttClient.publish("test_queue", uidString.c_str());
    Serial.print("Published UID: ");
    Serial.println(uidString);
    lastUID = uidString;
    lastCardPublishTime = currentMillis;
  }
  // Kartı durdur (halt) ederek tekrar okuma yapılmasını önle
  mfrc522.PICC_HaltA();
}

void connectToMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting to RabbitMQ (MQTT)...");
    // Benzersiz bir Client ID oluştur (duplicate ID hatasını önlemek için)
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
