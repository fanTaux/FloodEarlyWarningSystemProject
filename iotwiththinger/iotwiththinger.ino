#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h> // "PubSubClient" by Nick O'Leary
#include <Wire.h>
#include "DHT.h"          // "DHT sensor library" dan "Adafruit Unified Sensor" by Adafruit

// ================= KONEKSI & KREDENSIAL =================
const char* ssid = "Si";           
const char* password = "aaabaaaa"; 

// Kredensial Thinger.io
const char* thinger_username = "<usn>";      // Username Thinger.io
const char* device_id        = "<dvid>";     // Device ID di Thinger.io
const char* device_credentials = "<dvcr>";   // Password Device

// Bot Telegram
#define BOTtoken "<bot:token>"
#define CHAT_ID "<chatid>"

// Server MQTT Thinger.io
const char* mqtt_server = "backend.thinger.io";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// ================= TOPIK MQTT =================
const char* mqtt_topic = "flood/iothujan3/data";

// ================= KONFIGURASI SENSOR =================
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

const int TRIGPIN = 18;
const int ECHOPIN = 19;
const int rainPin = 39;
const int pin_buzzer = 23;

// Variable mengukur Ketinggian air
const int h_default = 9;  // tinggi awal air
const int s_default = 14; // jarak awal dari sensor ke permukaan air
int h_now = 0;            // ketinggian air jika mengalami perubahan

// Variabel Data
int jarak;
float h, t;
int rainValue = 0;

// Timer
unsigned long previousMillis = 0;
const long interval = 20000;

// ================= SETUP WIFI =================
void setup_wifi() {
  delay(10);
  Serial.print("\n[WIFI] Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n[WIFI] Connected!");
  Serial.print("[WIFI] IP Address: ");
  Serial.println(WiFi.localIP());
}

// ================= RECONNECT MQTT =================
void reconnect() {
  while (!client.connected()) {
    Serial.print("[MQTT] Connecting to Thinger.io...");
  
    if (client.connect(device_id, thinger_username, device_credentials)) {
      Serial.println(" Connected!");
      Serial.print("[MQTT] Client ID: ");
      Serial.println(device_id);
      Serial.print("[MQTT] Publishing to: ");
      Serial.println(mqtt_topic);
    } else {
      Serial.print(" Failed! RC=");
      Serial.print(client.state());
      Serial.println(" Retry in 5s...");
      delay(5000);
    }
  }
}

// ================= NOTIFICATION TO TELEGRAM BOT =================
bool sendTelegramMessage(int tinggi_air) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  String url = String("https://api.telegram.org/bot") + BOTtoken + "/sendMessage";
  String message;

  message = "Emergency Alert! \nTinggi Air Naik!! \nTinggi Sekarang: " + String(h_now) ;

  String payload = "{\"chat_id\":\"" + String(CHAT_ID) + "\",\"text\":\"" + message + "\"}";

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println(F("Connection to Telegram failed."));
    return false;
  }

  client.println("POST /bot" + String(BOTtoken) + "/sendMessage HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(payload.length()));
  client.println();
  client.println(payload);

  unsigned long startTime = millis();
  while (client.connected() || client.available()) {
    if (client.available()) {
      return true; // Pesan terkirim
    }
    if (millis() - startTime > 5000) break;
  }
  return false; // tidak ada respons atau gagal
}

// ================= SETUP =================
void setup() {
  Serial.begin(9600);

  Wire.begin(); 
  dht.begin();
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);
  pinMode(pin_buzzer, OUTPUT);
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(512); // Buffer size untuk JSON
  
  Serial.println("\n[SYSTEM] Ready!");
  Serial.print("[CONFIG] Topic: ");
  Serial.println(mqtt_topic);
  Serial.println("========================================\n");
}

// ================= MAIN LOOP =================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    // ========== BACA SENSOR ==========
    // HC-SR04 (Ultrasonic)
    digitalWrite(TRIGPIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIGPIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGPIN, LOW);
    long duration = pulseIn(ECHOPIN, HIGH);
    jarak = duration / 58;
    
    // Hitung ketinggian air
    if(jarak > 210) {
      h_now = 9;
    } else {
      h_now = h_default + (s_default - jarak);
    }

    // DHT11
    h = dht.readHumidity();
    t = dht.readTemperature();
    
    rainValue = analogRead(rainPin);

    // Status Hujan
    String rainStatus;
    if (rainValue == -1) {
      rainStatus = "Error";
    } else if (rainValue > 2500) {
      rainStatus = "Kering";
    } else {
      rainStatus = "Hujan";
    }

    // Jika tinggi naik, kirim notif buzzer nyala
    if (h_now >= 12){
      sendTelegramMessage(h_now);
      for (int i=0; i<4; i++){
        digitalWrite(pin_buzzer,HIGH);
        delay(250);
        digitalWrite(pin_buzzer,LOW);
        delay(250);
      }
    }
    
    // ========== VALIDASI & PUBLISH ==========
    if (!isnan(t) && !isnan(h) && jarak > 0 && jarak < 400) {
      
      // Format JSON
      // Thinger.io bucket otomatis membuat kolom dari key JSON
      String payload = "{";
      payload += "\"suhu\":" + String(t, 1) + ",";
      payload += "\"kelembapan\":" + String(h, 1) + ",";
      payload += "\"tinggi_air\":" + String(h_now) + ",";
      payload += "\"hujan_val\":" + String(rainValue) + ",";
      payload += "\"hujan_status\":\"" + rainStatus + "\"";
      payload += "}";
      
      // Publish ke Thinger.io MQTT Broker
      if (client.publish(mqtt_topic, payload.c_str())) {
        Serial.println("[SEND] ✓ Data sent successfully");
        Serial.print("[DATA] ");
        Serial.println(payload);
      } else {
        Serial.println("[ERROR] ✗ Failed to publish");
      }
    }
    Serial.println("---");

  }
}
