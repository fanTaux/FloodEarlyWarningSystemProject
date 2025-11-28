/*
 * Program Monitoring ESP32 dengan HiveMQ MQTT
 * Integrasi: HC-SR04, DHT11, I2C Sensor (Rain/ADC)
 * * Library yang dibutuhkan (Install via Library Manager):
 * 1. "PubSubClient" by Nick O'Leary
 * 2. "DHT sensor library" by Adafruit
 * 3. "Adafruit Unified Sensor"
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "DHT.h"

// ================= KONFIGURASI WIFI & MQTT =================
const char* ssid = "NAMA_WIFI_ANDA";        // Ganti dengan nama WiFi
const char* password = "PASSWORD_WIFI_ANDA"; // Ganti dengan password WiFi

// Konfigurasi HiveMQ Broker Public (Diupdate sesuai gambar)
const char* mqtt_server = "mqtt-dashboard.com"; 

// NOTE: Tetap gunakan 1883 untuk ESP32 (TCP). 
// Port 8884 pada gambar adalah untuk WebSocket (Browser/Web), bukan untuk ESP32.
const int mqtt_port = 1883; 

WiFiClient espClient;
PubSubClient client(espClient);

// Topik MQTT (Gunakan nama unik agar tidak bentrok dengan orang lain)
const char* topic_suhu = "project/rumah/suhu";
const char* topic_hujan = "project/rumah/hujan";
const char* topic_jarak = "project/rumah/jarak";

// ================= KONFIGURASI SENSOR =================
// --- DHT11 ---
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// --- HC-SR04 ---
const int TRIGPIN = 18;
const int ECHOPIN = 19;

// --- I2C Sensor (Rain/ADC) ---
#define I2C_ADDRESS 0x27 

// Variabel Data
long timer_sensor;
int jarak;
float h, t;
int rainValue = 0;

// Variabel untuk Non-blocking Delay (Millis)
unsigned long previousMillis = 0;
const long interval = 2000; // Kirim data setiap 2 detik

// ================= SETUP WIFI =================
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Menghubungkan ke ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi Terhubung");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ================= RECONNECT MQTT =================
void reconnect() {
  // Loop sampai terhubung kembali
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT Broker...");
    
    // Buat Client ID Unik (agar tidak ditendang broker)
    // PENTING: Jangan gunakan ClientID yang sama persis dengan yang di Web Dashboard (gambar),
    // karena satu ID hanya boleh untuk satu koneksi. Biarkan ESP32 generate ID random.
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    // Coba connect
    if (client.connect(clientId.c_str())) {
      Serial.println("Terhubung ke HiveMQ!");
      // Jika ingin subscribe topik (untuk menerima perintah), tambahkan di sini:
      // client.subscribe("project/lampu/switch");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

// ================= SETUP UTAMA =================
void setup() {
  Serial.begin(9600);
  
  // 1. Init Sensor
  Wire.begin(); 
  dht.begin();
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);

  // 2. Init Koneksi
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

// ================= LOOP UTAMA =================
void loop() {
  // Pastikan koneksi MQTT terjaga
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Penting untuk handle packet MQTT

  // Gunakan millis() pengganti delay() agar koneksi tidak putus
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // --- 1. BACA DATA SENSOR ---
    
    // HC-SR04
    digitalWrite(TRIGPIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIGPIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGPIN, LOW);
    long duration = pulseIn(ECHOPIN, HIGH);
    jarak = duration / 58;

    // DHT11
    h = dht.readHumidity();
    t = dht.readTemperature();

    // I2C Rain Sensor
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(0); 
    Wire.endTransmission();
    Wire.requestFrom(I2C_ADDRESS, 1);
    if (Wire.available()) {
      rainValue = Wire.read();
    } else {
      rainValue = -1;
    }

    // Interpretasi Status Hujan
    String rainStatus;
    if (rainValue == -1) rainStatus = "Error";
    else if (rainValue > 200) rainStatus = "Kering";
    else if (rainValue > 100) rainStatus = "Rintik";
    else rainStatus = "Hujan";

    // --- 2. TAMPILKAN DI SERIAL (DEBUG) ---
    Serial.println("--- Update Data ---");
    Serial.print("Jarak: "); Serial.print(jarak); Serial.println(" cm");
    if (!isnan(t)) {
      Serial.print("Suhu: "); Serial.print(t); Serial.println(" C");
    }
    Serial.print("Hujan Val: "); Serial.print(rainValue); Serial.println(" (" + rainStatus + ")");

    // --- 3. KIRIM KE HIVEMQ (PUBLISH) ---
    // MQTT butuh data dalam bentuk char array (string C-style)
    
    char msg[50];
    
    // Kirim Jarak
    snprintf(msg, 50, "%d", jarak);
    client.publish(topic_jarak, msg);

    // Kirim Suhu (Hanya jika valid)
    if (!isnan(t)) {
      snprintf(msg, 50, "%.2f", t);
      client.publish(topic_suhu, msg);
    }

    // Kirim Data Hujan (Gabungan Value dan Status)
    // Format kirim JSON sederhana agar mudah dibaca dashboard
    String jsonHujan = "{\"val\":" + String(rainValue) + ", \"status\":\"" + rainStatus + "\"}";
    // Konversi String ke Char Array untuk MQTT
    char msgRain[100];
    jsonHujan.toCharArray(msgRain, 100);
    client.publish(topic_hujan, msgRain);

    Serial.println("Data terkirim ke MQTT Broker");
  }
}
