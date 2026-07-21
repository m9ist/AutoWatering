# Автоматическая поливка растений

Система автоматического полива комнатных растений на базе Arduino Mega2560 + ESP8266. Поддерживает до 16 растений, управляется через Telegram, собирает данные с датчиков влажности почвы и отображает их на дисплее; логи и графики — в Grafana на домашнем сервере.

## Текущее состояние

- Проект в боевой эксплуатации (3–5 растений подключено)
- Управление поливом через Telegram-бота **aw-server** на домашнем сервере (`server/` этой репы)
- ESP — headless MQTT-шлюз: единственное соединение — локальный Mosquitto (см. [ADR-0001](docs/adr/0001-local-mqtt-broker.md))
- Логи ESP и метрики (температура, влажность воздуха, RAM, влажность почвы по растениям) — в Loki/Grafana, дашборд «Автополив»
- Уведомления в чат о переходах ESP онлайн/офлайн (LWT брокера)
- Ручной полив через кнопки на плате; ежедневный автополив — по команде `/daily`
- Диагностика подключения клапанов через датчик тока ACS712 (`/checkvalves`, кнопка на плате, статус в отчёте о каждом поливе)
- ⚠️ MQTT-прошивка ESP собрана и оттестирована native-тестами, но **на железо ещё не залита** — живые проверки по тикетам [#13](https://github.com/m9ist/AutoWatering/issues/13)/[#16](https://github.com/m9ist/AutoWatering/issues/16)/[#17](https://github.com/m9ist/AutoWatering/issues/17) впереди

## Архитектура

```
[Telegram]        [Grafana/Loki (дашборд «Автополив»)]
     │                      ▲
     ▼                      │ логи + метрики
[aw-server]  ── Python-сервис на домашнем серваке (код: server/)
     │  команды aw/cmd ↓ · логи aw/log/# ↑ · стейт aw/state ↑ · события aw/event ↑
     ▼
[Mosquitto]  ── локальный MQTT-брокер (LAN, user/pass)
     ▲
     │ единственное внешнее соединение ESP
[ESP8266]    ── headless: WiFi, NTP, OTA, MQTT
     │  UART, текстовый JSON-протокол
[Arduino Mega2560] ── насос, клапаны, датчики, дисплей, RTC, кнопки, SD-лог
```

**Arduino Mega2560** управляет всем железом, хранит стейт в EEPROM, исполняет команды полива, пишет логи на SD (офлайн-фолбэк).

**ESP8266** — тонкий шлюз: форвардит Команды из `aw/cmd` в UART (с валидацией границ), публикует кадр стейта Mega в `aw/state` как есть (retained), шлёт логи в `aw/log/esp` (при обрыве — RAM-кольцо с досылкой), держит LWT `aw/online`. Прошивается по OTA.

**aw-server** (Python, `server/`) — телеграм-бот (whitelist чатов), мост логов в Loki, метрики из стейта, уведомления о статусе ESP. Деплой — docker-стек в приватной инфра-репе homelab.

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

Бот живёт в aw-server; команды принимаются только из whitelist-чатов.

| Команда | Описание |
|---------|----------|
| `/water plantX Yml` | Полить растение X на Y мл. Пример: `/water plant3 50ml` |
| `/config plantX Yml` | Установить дневную норму полива. Пример: `/config plant2 20ml` |
| `/state` | Последний стейт из retained `aw/state` (влажность, температура, свободная память) |
| `/daily` | Запустить ежедневный цикл полива вручную |
| `/checkvalves` | Проверить подключение всех активных клапанов (по дельте тока ACS712) |
| `/help` | Справка по командам |

Графики влажности/климата — в Grafana (ASCII-графики в чате выпилены, issue [#20](https://github.com/m9ist/AutoWatering/issues/20)). Периодическая сводка стейта отключена (`HOURLY_SUMMARY_INTERVAL_S=0` в деплое).

## Начало работы

**Среда разработки:** [VS Code](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/platformio-ide)

Проект содержит две прошивки в `platformio.ini`:
- `megaatmega2560` — для Arduino Mega
- `esp` — для ESP8266 (загрузка по OTA на адрес `192.168.1.96`)

**Обязательные шаги перед сборкой:**

1. Создать файл `src/SecretHolder.cpp` (вне git) и реализовать методы:
   ```cpp
   String ssid()         { return "your_wifi_ssid"; }
   String wifiPasswork() { return "your_wifi_password"; }
   String mqttUser()     { return "esp"; }
   String mqttPassword() { return "<пароль учётки esp из sops стека mqtt>"; }
   ```

**Native-тесты** (парсер команд, RAM-кольцо логов, обработчик `aw/cmd`) — способ запуска описан в комментарии в конце `platformio.ini`.

**aw-server** (`server/`): Python 3.12, тесты `python -m pytest`; конфиг через env (см. `server/aw_server/config.py`). Деплой — стек `watering` в приватной репе homelab (там же брокер, стек `mqtt`).

## Протокол

**Arduino ↔ ESP (UART, JSON с ключом `"c"`):**

| Направление | Команда | Описание |
|-------------|---------|----------|
| ESP → Arduino | `esp_water` | Полить растение |
| ESP → Arduino | `esp_plant_conf` | Настроить дневную норму |
| ESP → Arduino | `esp_daily` | Запустить ежедневный полив |
| ESP → Arduino | `esp_check_valves` | Проверить подключение клапанов |
| ESP → Arduino | `esp_ntp` | Синхронизировать время |
| Arduino → ESP | `state` | Обновлённый стейт системы |
| Arduino → ESP | `arduino_tg` | Сообщение для человека (уходит Событием в `aw/event`) |

**MQTT (ESP ↔ aw-server, брокер Mosquitto):**

| Топик | Направление | Что | Retained |
|-------|-------------|-----|----------|
| `aw/cmd` | aw-server → ESP | JSON UART-протокола, passthrough (ESP валидирует границы) | нет |
| `aw/state` | ESP → aw-server | кадр `state` от Mega как есть | **да** |
| `aw/log/esp` | ESP → aw-server | строки лога `{ts, lvl, msg}` → Loki | нет |
| `aw/event` | ESP → aw-server | `{type, text}` — для Telegram | нет |
| `aw/online` | LWT | `1`/`0` — статус ESP (уведомления + панель в Grafana) | да |

## Известные проблемы и ограничения

- Физические соединения на pogo-pin разъёмах нестабильны — клапаны могут выпасть
- Ежедневный автозапуск полива по расписанию пока не реализован (только через `/daily`)
- Команды при офлайн-ESP не доставляются (clean-session + QoS0) — бот честно предупреждает в ACK
- Из двух насосов всегда используется запасной (`PIN_POMP_SPARE`), схема ротации моторов не реализована
- У брокера нет ACL — доводка в issue [#19](https://github.com/m9ist/AutoWatering/issues/19)

## Команда проекта

- tg [@m9ist](https://t.me/m9ist)

---
*Используйте на свой страх и риск. Автор не компетентен в электронике.*
