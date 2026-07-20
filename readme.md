# Автоматическая поливка растений

Система автоматического полива комнатных растений на базе Arduino Mega2560 + ESP8266. Поддерживает до 16 растений, управляется через Telegram, собирает данные с датчиков влажности почвы и отображает их на дисплее.

## Текущее состояние

- Проект в боевой эксплуатации (3–5 растений подключено)
- Управление поливом через Telegram-бота
- Сбор и отображение данных влажности почвы, температуры и влажности воздуха
- Ручной полив через кнопки на плате
- Ежедневный автополив по команде из Telegram (`/daily`)
- Графики влажности по данным за ~3 дня
- Диагностика подключения клапанов через датчик тока ACS712 (`/checkvalves`, кнопка на плате, статус в отчёте о каждом поливе)

## Архитектура

Два микроконтроллера на одной плате, общаются через UART по текстовому json протоколу:

```
[Arduino Mega2560]  <--UART-->  [ESP8266]
  - насос и клапаны               - WiFi
  - датчики влажности почвы       - Telegram-бот
  - датчик расхода воды           - веб-интерфейс (SettingsGyver)
  - датчик тока                   - синхронизация времени (NTP)
  - дисплей, RTC, кнопки          - Яндекс IoT Core (в разработке)
```

**Arduino Mega2560** управляет всем железом, хранит стейт в EEPROM, исполняет команды полива.

**ESP8266** обеспечивает связь с внешним миром: принимает команды из Telegram, периодически отправляет стейт (каждый час), прошивается по воздуху (OTA).

## Компоненты

| Компонент | Модель | Описание |
|-----------|--------|----------|
| Плата | Mega2560+WiFi-R3-AT328-ESP8266-32MB-CH340G | Arduino Mega + ESP8266 на одной плате |
| DC-DC преобразователь | LM2596S | 2 шт. Питание от внешнего БП 20V: один понижает до 5V (клапаны, электроника), второй до 12V (насос) |
| Дисплей | IPS TFT ST7789 240×240 (ZJY-IPS130-V2.0) | Статус системы. [Схема подключения](https://simple-circuit.com/wp-content/uploads/2019/06/arduino-st7789-color-tft-240x240-pixel-interfacing-circuit.png) |
| RTC | DS1302 (чип DS1307) | Часы реального времени. [Документация](https://www.nookery.ru/ds1302-in-arduino/) |
| Насос | R385 + драйвер MX1508 (чип L298N) | Перистальтический насос. [Документация L298N](https://robotchip.ru/obzor-drayvera-motora-na-l298n/) |
| Клапаны | DC 5V HUXUAN электромагнитные | 16 шт, по одному на растение. Драйвер: WAVGAT ULN2003 5V 4-phase |
| Управление клапанами | 74HC595 сдвиговые регистры (3 шт) | Управление 16+ клапанами через 3 пина. [Урок](https://alexgyver.ru/lessons/74hc595/), [подключение](https://uscr.ru/kak-podklyuchit-sdvigoviy-registr-k-arduino/) |
| Датчики влажности почвы | HD-38 (SZYTF Soil Moisture Sensor) | 16 шт, аналоговые, ёмкостные. [Пример кода](https://github.com/vrxfile/test_arduino_sensors_modules/blob/master/capacitive_moisture_test/capacitive_moisture_test.ino), [описание](https://myduino.com/product/jhs-273/) |
| Мультиплексер | CD74HC4067 | 16-канальный, для датчиков влажности и кнопок. [Документация](https://arduinolab.pw/index.php/2017/07/17/16-kanalnyj-analogovyj-multipleksor-cd74hc4067/) |
| Датчик расхода воды | HESAI YF-S401 | Учёт пролитой воды в мл. [Документация](https://wiki.iarduino.ru/page/sensor-water-flow/) |
| Датчик тока | ACS712 20A | Диагностика клапанов |
| Датчик темп./влажности | GY-SHT30-D (чип SHT30) | Температура и влажность воздуха |
| Датчик уровня воды | XKC-Y25-NPN 5–12V | Бесконтактный. [Документация](https://wiki.amperka.ru/products:sensor-liquid-level-contactless) |
| SD-ридер | hw-125 (чип B108) | Логирование |

## Команды Telegram

| Команда | Описание |
|---------|----------|
| `/water plantX Yml` | Полить растение X на Y мл. Пример: `/water plant3 50ml` |
| `/config plantX Yml` | Установить дневную норму полива. Пример: `/config plant2 20ml` |
| `/state` | Получить текущий стейт (влажность, температура, свободная память) |
| `/graphs` | ASCII-графики влажности почвы за последние ~3 дня |
| `/daily` | Запустить ежедневный цикл полива вручную |
| `/checkvalves` | Проверить подключение всех активных клапанов (по дельте тока ACS712) |
| `/help` | Справка по командам |

## Начало работы

**Среда разработки:** [VS Code](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/platformio-ide)

Проект содержит две прошивки в `platformio.ini`:
- `megaatmega2560` — для Arduino Mega
- `esp` — для ESP8266 (загрузка по OTA на адрес `192.168.1.96`)

**Обязательные шаги перед сборкой:**

1. Создать файл `src/SecretHolder.cpp` и реализовать методы:
   ```cpp
   String ssid()              { return "your_wifi_ssid"; }
   String wifiPasswork()      { return "your_wifi_password"; }
   String publicCert()        { return "-----BEGIN CERTIFICATE-----\n..."; }
   String privateCert()       { return "-----BEGIN RSA PRIVATE KEY-----\n..."; }
   String getDeviceId()       { return "your_yandex_iot_device_id"; }
   String getTelegramBotToken() { return "your_telegram_bot_token"; }
   ```

## Интеграция с Яндекс IoT (в разработке)

Планируется отправка данных в [Яндекс IoT Core](https://yandex.cloud/ru/docs/iot-core/quickstart) для интеграции с Алисой.

Генерация сертификатов (нужны отдельно для реестра и устройства):
```bash
openssl req -x509 -newkey rsa:4096 -keyout private-key.pem -out cert.pem -nodes -days 365 -subj "/CN=localhost"
```

Получение IAM-токена (актуален ~1 час):
```bash
curl --request POST \
  --data '{"yandexPassportOauthToken":"<OAuth-токен>"}' \
  https://iam.api.cloud.yandex.net/iam/v1/tokens
```

## Протокол Arduino ↔ ESP

JSON-сообщения содержат ключ `"c"` (команда). Основные команды:

| Направление | Команда | Описание |
|-------------|---------|----------|
| ESP → Arduino | `esp_water` | Полить растение |
| ESP → Arduino | `esp_plant_conf` | Настроить дневную норму |
| ESP → Arduino | `esp_daily` | Запустить ежедневный полив |
| ESP → Arduino | `esp_check_valves` | Проверить подключение клапанов |
| ESP → Arduino | `esp_ntp` | Синхронизировать время |
| Arduino → ESP | `state` | Обновлённый стейт системы |
| Arduino → ESP | `arduino_tg` | Отправить сообщение в Telegram |

## Известные проблемы и ограничения

- Физические соединения на pogo-pin разъёмах нестабильны — клапаны могут выпасть
- Ежедневный автозапуск полива по расписанию пока не реализован (только через `/daily`)
- Интеграция с Яндекс IoT отключена (`sendMessageToIOT` закомментирована)
- Из двух насосов всегда используется запасной (`PIN_POMP_SPARE`), схема ротации моторов не реализована

## Команда проекта

- tg [@m9ist](https://t.me/m9ist)

---
*Используйте на свой страх и риск. Автор не компетентен в электронике.*
