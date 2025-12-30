#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include "DHTesp.h"

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* host = "flownix-backend.onrender.com";
const char* path = "/api/SensorReading";

const char* temperatureSensorId = "008ee850-a61f-4aee-aa20-060897b6d6a4";
const char* waterLevelSensorId  = "9746cc6d-f517-44ed-9197-09988bf9f76c";

#define DHTPIN 15
DHTesp dht;

#define TRIG_PIN 26
#define ECHO_PIN 27

LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 30000; // 30s

IPAddress backendIp;

void lcdStatus(const String& a, const String& b = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(a.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(b.substring(0, 16));
}

long readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return (long)(duration * 0.034 / 2.0);
}

void warmupServer() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60000);

  HTTPClient http;
  http.setTimeout(60000);

  String url = String("https://") + host + "/";
  if (http.begin(client, url)) {
    http.GET();
    http.end();
  }
}

bool postValue(const char* sensorId, float value) {
  StaticJsonDocument<128> doc;
  doc["sensorId"] = sensorId;
  doc["value"] = value;

  String body;
  serializeJson(doc, body);

  Serial.print("[HTTP] Payload: ");
  Serial.println(body);

  for (int attempt = 1; attempt <= 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60000);

    HTTPClient http;
    http.setTimeout(60000);

    String url = String("https://") + backendIp.toString() + path;

    if (!http.begin(client, url)) {
      http.end();
      delay(700);
      continue;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("Host", host); 

    Serial.print("[HTTP] POST ");
    Serial.println(url);

    int code = http.POST(body);

    if (code > 0) {
      Serial.print("[HTTP] Code: ");
      Serial.println(code);
      Serial.print("[HTTP] Resp: ");
      Serial.println(http.getString());
      http.end();
      return (code >= 200 && code < 300);
    }

    Serial.print("[HTTP] Error: ");
    Serial.println(code);
    http.end();

    delay(1200 * attempt);
  }

  return false;
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  dht.setup(DHTPIN, DHTesp::DHT22);

  lcd.init();
  lcd.backlight();
  lcdStatus("Flownix system", "Starting...");
  delay(800);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  lcdStatus("Connecting WiFi", "Wokwi-GUEST");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  lcdStatus("WiFi connected", WiFi.localIP().toString());
  delay(800);

  if (!WiFi.hostByName(host, backendIp)) {
    Serial.println("[DNS] FAIL");
    backendIp = IPAddress(216, 24, 57, 7); 
  }
  Serial.print("[DNS] Using IP: ");
  Serial.println(backendIp);

  warmupServer();
  lcd.clear();
}

void loop() {
  TempAndHumidity th = dht.getTempAndHumidity();
  float temperature = th.temperature;
  float humidity = th.humidity;

  long distance = readDistanceCM();

  // LCD
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temperature, 1);
  lcd.print((char)223);
  lcd.print("C ");
  lcd.print("H:");
  lcd.print((int)humidity);
  lcd.print("% ");

  lcd.setCursor(0, 1);
  lcd.print("Water:");
  lcd.print(distance);
  lcd.print("cm   ");


  if (millis() - lastUploadTime >= uploadInterval) {
    lcdStatus("Sending data...", "");

    bool ok1 = postValue(temperatureSensorId, temperature);

    delay(3000);

    bool ok2 = postValue(waterLevelSensorId, (float)distance);

    if (ok1 && ok2) lcdStatus("Data sent OK!", "");
    else lcdStatus("Send failed!", "");

    lastUploadTime = millis();
    delay(1200);
    lcd.clear();
  }

  delay(1500);
}