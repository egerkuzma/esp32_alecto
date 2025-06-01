#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WeatherStationDataRx.h>

// === Настройка Wi-Fi ===
const char* WIFI_SSID = "TP-Link_5BAF";
const char* WIFI_PASS = "53541437";

// URL до вашего PHP-скрипта
const char* SERVER_URL = "http://my.host/weather_log.php";


// DATA-пин, куда подключён датчик Alecto V1 (GPIO 5)
constexpr uint8_t DATA_PIN = 5;
WeatherStationDataRx wsdr(DATA_PIN);

// Переменные для хранения «последнего отправленного» пакета
uint16_t last_id       = 0xFFFF;    // инициализируем заведомо «неправильным»
float   last_temp      = -1000.0f;  // температура выйдет за эти рамки
uint8_t last_humidity  = 0xFF;      // влажность 0–100, 0xFF значит «не было»
uint8_t last_battery   = 0xFF;      // 0 или 1, 0xFF – «не было»

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("Alecto V1 HTTP-Client ESP32 (фильтр повторов)"));

  // Подключаемся к Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Подключение к Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi подключён, IP: ");
  Serial.println(WiFi.localIP());

  // Запускаем приём от датчика
  wsdr.begin();
}

void loop() {
  // wsdr.readData() возвращает:
  //   'T' – пакет «температура+влажность»
  //   'W' – ветер
  //   'R' – дождь
  //   'B' – нажатие кнопки
  //     0 – ничего нового
  char t = wsdr.readData();
  if (!t) {
    // новые данные отсутствуют
    return;
  }

  // Обрабатываем только «T» (температура+влажность)
  if (t == 'T') {
    uint16_t sensor_id    = wsdr.sensorID();
    float   temperature   = wsdr.readTemperature(); // в °C
    uint8_t humidity      = wsdr.readHumidity();    // в %
    uint8_t battery_level = wsdr.batteryStatus() ? 1 : 0;
    uint8_t sensor_ch     = 0; // без channel()

    // Проверяем, отличается ли хоть одно поле от «последнего отправленного»
    bool isNew =
      (sensor_id    != last_id)    ||
      (fabs(temperature - last_temp) > 0.05f) ||  // допускаем небольшое «плавающее» расхождение
      (humidity      != last_humidity) ||
      (battery_level != last_battery);

    if (isNew) {
      // Запоминаем новые «отправленные» значения
      last_id       = sensor_id;
      last_temp     = temperature;
      last_humidity = humidity;
      last_battery  = battery_level;

      // Локальный вывод в Serial
      Serial.printf("Новый пакет: ID:%u  T:%.1f°C  H:%u%%  Bat:%u\n",
                    sensor_id, temperature, humidity, battery_level);

      // Отправляем HTTP POST на сервер
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(SERVER_URL);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        // Формируем тело POST: id, ch, t, h, bat
        String postData = "";
        postData += "id="  + String(sensor_id);
        postData += "&ch=" + String(sensor_ch);
        postData += "&t="  + String(temperature, 1);
        postData += "&h="  + String(humidity);
        postData += "&bat="+ String(battery_level);

        int httpCode = http.POST(postData);
        if (httpCode > 0) {
          String payload = http.getString();
          Serial.printf("POST код: %d, ответ: %s\n", httpCode, payload.c_str());
        } else {
          Serial.printf("Ошибка HTTP: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
      } else {
        Serial.println("Wi-Fi не подключён, отправка пропущена");
      }

      // Маленькая задержка, чтобы дать время на UART и HTTP-запрос
      delay(1000);
    }
    else {
      // Это повтор того же пакета — пропускаем
      // Можно вывести в Serial, если нужно отслеживать, что пришёл «дубликат»
      Serial.println("Дубликат пакета, отправку пропускаем");
    }
  }
}
