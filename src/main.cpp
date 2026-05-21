#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WeatherStationDataRx.h>

#include "secrets.h"  // defines WIFI_SSID, WIFI_PASS, SERVER_URL

constexpr uint8_t DATA_PIN = 5;
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;
constexpr uint16_t POST_COOLDOWN_MS = 1000;

WeatherStationDataRx wsdr(DATA_PIN);

// last forwarded packet — initialised to sentinels that no real reading hits
uint16_t last_id       = 0xFFFF;
float    last_temp     = -1000.0f;
uint8_t  last_humidity = 0xFF;
uint8_t  last_battery  = 0xFF;

static void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Wi-Fi reconnecting");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 40) {  // ~20 s
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connect failed");
  }
}

static bool postReading(uint16_t id, uint8_t ch, float temp, uint8_t hum, uint8_t bat) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi down — skipping POST");
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, SERVER_URL)) {
    Serial.println("http.begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body;
  body.reserve(64);
  body += "id=";  body += id;
  body += "&ch="; body += ch;
  body += "&t=";  body += String(temp, 1);
  body += "&h=";  body += hum;
  body += "&bat="; body += bat;

  const int code = http.POST(body);
  if (code > 0) {
    Serial.printf("POST %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.printf("POST error: %s\n", http.errorToString(code).c_str());
  }
  http.end();
  return code > 0 && code < 400;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\nAlecto V1 HTTP forwarder (ESP32)");
  WiFi.mode(WIFI_STA);
  ensureWiFi();
  wsdr.begin();
}

void loop() {
  const char t = wsdr.readData();
  if (!t) {
    delay(10);
    return;
  }
  if (t != 'T') return;

  const uint16_t id     = wsdr.sensorID();
  const float    temp   = wsdr.readTemperature();
  const uint8_t  hum    = wsdr.readHumidity();
  const uint8_t  bat    = wsdr.batteryStatus() ? 1 : 0;
  const uint8_t  ch     = 0;

  const bool isNew =
      (id != last_id) ||
      (fabsf(temp - last_temp) > 0.05f) ||
      (hum != last_humidity) ||
      (bat != last_battery);

  if (!isNew) {
    Serial.println("duplicate, skipping");
    delay(POST_COOLDOWN_MS);
    return;
  }

  last_id = id;
  last_temp = temp;
  last_humidity = hum;
  last_battery = bat;
  Serial.printf("new: id=%u t=%.1fC h=%u%% bat=%u\n", id, temp, hum, bat);

  ensureWiFi();
  postReading(id, ch, temp, hum, bat);
  delay(POST_COOLDOWN_MS);
}
