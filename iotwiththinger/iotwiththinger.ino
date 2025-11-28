/*
 * Program Monitoring ESP32 untuk Thinger.io (via MQTT)
 * Integrasi: HC-SR04, DHT11, I2C Sensor (Rain/ADC)
 * * PENTING:
 * Karena Anda memilih "MQTT Device" di Thinger.io, kita menggunakan
 * protokol MQTT standar (backend.thinger.io) port 1883.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "DHT.h"

// ================= KONFIGURASI WIFI =================
const char* ssid = "faris";        
const char* password = "kirakira"; 

// ================= KONFIGURASI THINGER.IO =================
// Masukkan data dari Dashboard Thinger.io Anda di sini:
const char* thinger_username = "fanTaux"; // Username akun Thinger.io Anda
const char* device_id        = "esp32_saya";      // Contoh: "esp32_saya" (dari form Add Device)
const char* device_credentials = "GPVUA9+8Xodzvt29"; // Password device yang Anda buat

const char* mqtt_server = "backend.thinger.io";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Topik Publish Thinger.io
// Format: v3/<username>/devices/<device_id>/resources/<resource_name>
// Kita akan mengirim semua data ke resource bernama "data_sensor"
String topic_pub = String("v3/") + thinger_username + "/devices/" + device_id + "/resources/data_sensor";

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
int jarak;
float h, t;
int rainValue = 0;

// Variabel Timer
unsigned long previousMillis = 0;
const long interval = 2000; 

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
  Serial.println("\nWiFi Terhubung");
}

// ================= RECONNECT MQTT (THINGER.IO) =================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke Thinger.io MQTT...");
    
    // Thinger.io butuh autentikasi: (ClientID, Username, Password)
    // ClientID = Device ID
    // User = Username Akun
    // Pass = Device Credentials
    if (client.connect(device_id, thinger_username, device_credentials)) {
      Serial.println("Terhubung!");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi 5 detik...");
      delay(5000);
    }
  }
}

// ================= SETUP UTAMA =================
void setup() {
  Serial.begin(9600);
  
  Wire.begin(); 
  dht.begin();
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

// ================= LOOP UTAMA =================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // --- 1. BACA DATA SENSOR ---
    // HC-SR04
    digitalWrite(TRIGPIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIGPIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIGPIN, LOW);
    long duration = pulseIn(ECHOPIN, HIGH);
    jarak = duration / 58;

    // DHT11
    h = dht.readHumidity();
    t = dht.readTemperature();

    // I2C Rain
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(0); Wire.endTransmission();
    Wire.requestFrom(I2C_ADDRESS, 1);
    rainValue = (Wire.available()) ? Wire.read() : -1;

    // Status Hujan
    String rainStatus = (rainValue == -1) ? "Error" : (rainValue > 200) ? "Kering" : (rainValue > 100) ? "Rintik" : "Hujan";

    // --- 2. DEBUG SERIAL ---
    Serial.printf("Jarak: %d cm | Suhu: %.1f C | Hujan: %d (%s)\n", jarak, t, rainValue, rainStatus.c_str());

    // --- 3. KIRIM KE THINGER.IO (JSON) ---
    // Thinger.io MQTT mengharapkan format JSON: {"key": value, "key2": value}
    // Note: Pastikan data valid (tidak NaN) sebelum dikirim
    
    if (!isnan(t) && !isnan(h)) {
      String payload = "{";
      payload += "\"suhu\":" + String(t) + ",";
      payload += "\"kelembapan\":" + String(h) + ",";
      payload += "\"jarak\":" + String(jarak) + ",";
      payload += "\"hujan_val\":" + String(rainValue) + ",";
      payload += "\"hujan_status\":\"" + rainStatus + "\"";
      payload += "}";

      // Konversi ke char array
      char msg[200];
      payload.toCharArray(msg, 200);

      // Publish ke topik Thinger.io
      client.publish(topic_pub.c_str(), msg);
      Serial.println("Data terkirim ke Thinger.io: " + payload);
    }
  }
}