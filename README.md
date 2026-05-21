# esp32_alecto

ESP32 sketch that listens for an Alecto V1 wireless weather sensor on a GPIO line and forwards each new reading to an HTTP endpoint as a form POST.

![platform](https://img.shields.io/badge/platform-ESP32-blue) ![framework](https://img.shields.io/badge/framework-Arduino-orange) ![license](https://img.shields.io/badge/license-MIT-green)

## What it does

The Alecto V1 sensor broadcasts on 433 MHz. With a cheap receiver wired into GPIO 5, the [WeatherStationDataRx](https://github.com/bbx10/WeatherStationDataRx) library decodes the packets. This sketch keeps the last forwarded reading in memory, de-duplicates incoming packets, and POSTs the new ones to a URL of your choice (`id`, `ch`, `t`, `h`, `bat` as form fields). A reference PHP receiver is included below.

It's a one-evening project — no OTA, no MQTT, no TLS. Use it on a trusted LAN.

## Hardware

| Part | Notes | Approx. cost |
| --- | --- | --- |
| ESP32 dev board (any Arduino-core variant) | DevKitC, Wemos, NodeMCU-32 | $5 |
| Alecto V1 outdoor sensor | Temperature + humidity, 433 MHz | $15 |
| 433 MHz RX module (RXB6 or similar) | Superheterodyne preferred over regen | $3 |

Wiring:

```
Alecto V1 (433 MHz)  ))) ))) (((  RXB6 DATA ──► GPIO 5 (ESP32)
                                       VCC ──► 3.3 V
                                       GND ──► GND
```

## Quickstart (PlatformIO)

```bash
git clone https://github.com/egerkuzma/esp32_alecto.git
cd esp32_alecto
cp include/secrets.h.example include/secrets.h
# edit include/secrets.h with your Wi-Fi + server URL
pio run -t upload
pio device monitor
```

Or in Arduino IDE: install the **WeatherStationDataRx** library via Library Manager, open `src/main.cpp` as a sketch (rename to `.ino`), and create a `secrets.h` next to it.

## Configuration

Edit `include/secrets.h` (gitignored). Three values:

| Define | Meaning |
| --- | --- |
| `WIFI_SSID` | Your network SSID |
| `WIFI_PASS` | Your network password |
| `SERVER_URL` | HTTP endpoint to POST readings to |

## How it works

1. `WeatherStationDataRx::readData()` returns a packet type (`'T'` = temp + humidity, others ignored here).
2. The sketch compares ID, temperature (±0.05 °C), humidity, and battery flag against the last forwarded reading.
3. If anything changed, it POSTs `id=…&ch=0&t=…&h=…&bat=…` to `SERVER_URL`.
4. Wi-Fi is checked before every POST and reconnected if dropped.

## Example PHP receiver

```php
<?php
// weather_log.php — minimal MySQL receiver.
$conn = new mysqli('localhost', getenv('DB_USER'), getenv('DB_PASS'), 'weather_db');
if ($conn->connect_error) { http_response_code(500); exit('DB down'); }

$fields = ['id','ch','t','h','bat'];
foreach ($fields as $f) if (!isset($_POST[$f])) { http_response_code(400); exit("missing $f"); }

$stmt = $conn->prepare(
  'INSERT INTO weather_log (sensor_id, channel, temperature, humidity, battery)
   VALUES (?, ?, ?, ?, ?)'
);
$stmt->bind_param('iidii',
  $_POST['id'], $_POST['ch'], $_POST['t'], $_POST['h'], $_POST['bat']
);
$stmt->execute();
echo 'OK';
```

Matching schema:

```sql
CREATE TABLE weather_log (
  id INT AUTO_INCREMENT PRIMARY KEY,
  sensor_id SMALLINT NOT NULL,
  channel TINYINT NOT NULL,
  temperature FLOAT NOT NULL,
  humidity TINYINT NOT NULL,
  battery TINYINT NOT NULL,
  ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

## Limitations

- Plain HTTP only — anyone on the LAN can read your readings or fake them.
- No persistence: a reboot resets the de-dup state, so the first packet after boot is always forwarded.
- WeatherStationDataRx receiving is timing-sensitive; cheap regen RX modules sometimes drop packets.
- Only `'T'` (temperature + humidity) packets are forwarded — wind/rain/button events are ignored.

## Credits

- [WeatherStationDataRx](https://github.com/bbx10/WeatherStationDataRx) — Alecto V1 decoder
- [arduino-esp32](https://github.com/espressif/arduino-esp32) — Arduino core for ESP32

## License

MIT — see [LICENSE](LICENSE).
