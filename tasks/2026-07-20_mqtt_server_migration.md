# Переезд ESP на локальный MQTT: логи в Loki, телеграм-бот на серваке

Дата: 2026-07-20. Решения зафиксированы в грилинг-сессии;
архитектурное — в [`../docs/adr/0001-local-mqtt-broker.md`](../docs/adr/0001-local-mqtt-broker.md),
термины — в [`../CONTEXT.md`](../CONTEXT.md).
Заменяет план [`2026-07-04_mqtt_dealgate_implementation_plan.md`](./2026-07-04_mqtt_dealgate_implementation_plan.md)
в части «ESP → Dealgate напрямую».

## Контекст

- Телеграм на ESP сейчас **не работает** (блокировки/VPN) — у системы нет канала
  управления, кроме железа. Это главный драйвер приоритетов.
- На серваке deb уже есть Loki + Alloy + Grafana (этап 2 мониторинга, 2026-07-18)
  и VPN-слой с failover (проблема доступа к Telegram API решена).
- Веб-морда Гравера (SettingsGyver) давала только лог 2500 байт в RAM,
  PlotStack и мёртвую кнопку — всё это заменяется серверным наблюдением.

## Архитектура

```
[Telegram]                    [Алиса — потом, на стороне сервака]
    │                                  │ (mosquitto-bridge в Dealgate или «Маша»)
    ▼                                  ▼
[aw-server, Python, docker]   ← код в server/ этой репы, деплой в homelab
    │  ├─ телеграм-бот (тот же токен, whitelist чатов)
    │  ├─ aw/log/#  → Loki push API  (метки stack=watering, service=esp)
    │  └─ aw/cmd ←→ aw/event, aw/state
    ▼
[Mosquitto]  ← homelab/stacks/mqtt: порт 1883 в LAN, user/pass в sops, без TLS,
    ▲          отдельные учётки для esp и aw-server
    │ PubSubClient, единственный брокер
[ESP8266]  ── headless: OTA + UART + MQTT
    │ UART (протокол не меняется)
[Mega2560] ── не трогаем: SD-логи и приём логов ESP на SD — как есть
```

## Решения (из грилинга)

| # | Вопрос | Решение |
|---|--------|---------|
| 1 | Веб-морда ESP | Нет. Headless, наблюдение в Grafana |
| 2 | Транспорт | MQTT — универсальная шина (логи + команды + будущая Алиса) |
| 3 | Брокеры | Один локальный Mosquitto; Алиса потом мостом на серваке (ADR-0001) |
| 4 | Телеграм-бот | Отдельный сервис aw-server; PicoClaw не трогаем |
| 5 | Гранулярность | Один сервис: бот + мост MQTT→Loki в одном процессе |
| 6 | Стек aw-server | Python (paho-mqtt + telegram-lib + requests в Loki) |
| 7 | Логи Mega / SD | Не трогаем: в Loki только логи ESP; SD-путь (в т.ч. ESP→Mega→SD) как есть |
| 8 | Команды | JSON-passthrough: `aw/cmd` несёт тот же JSON, что UART-протокол |
| 9 | Обрыв брокера | RAM-кольцо ~2–4 КБ с досылкой при реконнекте |
| 10 | Брокер деплой | Отдельный стек homelab/stacks/mqtt (общий для дома, Джарвис подключится к нему же) |
| 11 | Порядок | Сервак → ESP (см. этапы) |

Микро-решения:
- Часовые сводки стейта в Telegram планирует **бот** (из retained `aw/state`);
  `/state` отвечает бот без похода на ESP; таймер `timerStateSendTelegram` с ESP уходит.
- ASCII-графики остаются на ESP: `/graphs` → `aw/cmd` → ESP рендерит Graph.h →
  текст назад через `aw/event` → бот пересылает в чат.
- Метрики в Grafana — следующий шаг после логов: парсить из лог-строк
  (стейт публикуется как JSON — LogQL распарсит), отдельный пайплайн метрик не строим.

## Топики и форматы

| Топик | Направление | Payload | Retained |
|---|---|---|---|
| `aw/log/esp` | ESP → сервер | JSON `{ts, lvl, msg}` | нет |
| `aw/state` | ESP → сервер | JSON стейта (как в UART `state`) | **да** |
| `aw/event` | ESP → сервер | JSON `{type, text}` — для Telegram | нет |
| `aw/cmd` | сервер → ESP | JSON UART-протокола (`c`: esp_water, …) | нет |
| `aw/online` | LWT + on-connect | `1` / `0` | да |

Timestamp для Loki ставит aw-server при приёме; `ts` от ESP остаётся в теле строки.

## Этапы

### Этап 1. Брокер (homelab)
Стек `homelab/stacks/mqtt`: Mosquitto, 1883 в LAN, учётки esp/aw-server в sops.
**Критерий:** `mosquitto_pub`/`sub` с компа через LAN работают с паролем, аноним отлуплен.

### Этап 2. aw-server (эта репа, `server/`)
Python-сервис: подписка `aw/#`, пуш `aw/log/#` в Loki, телеграм-бот
(команды `/water`, `/config`, `/state`, `/graphs`, `/daily`, `/checkvalves`, `/help`,
whitelist чатов), публикация команд в `aw/cmd`, пересылка `aw/event` в чат,
часовые сводки из `aw/state`. Деплой-обвязка — в homelab.
**Критерий:** без ESP — руками опубликованная строка в `aw/log/esp` видна в Grafana
(«Логи — Loki», `{stack="watering"}`); `/water plant2 50ml` в чате порождает
корректный JSON в `aw/cmd`; строка в `aw/event` прилетает в чат.

### Этап 3. Прошивка ESP (одним заходом)
- Вырезать: SettingsGyver + `sets::Logger`, UniversalTelegramBot,
  `WiFiClientSecure`/BearSSL, Yandex IoT (`sendMessageToIOT`, сертификаты
  и мёртвые методы SecretHolder), DEVFULL, `updatedSettings`.
- Добавить: PubSubClient → Mosquitto (reconnect, LWT `aw/online`);
  логгер за прежним интерфейсом (Serial + UART→SD как раньше + MQTT
  с RAM-кольцом и досылкой, маркер «reconnected, N строк потеряно»);
  подписка `aw/cmd` (валидация bounds остаётся) → форвард в UART;
  публикация `aw/state` retained на каждый стейт, `aw/event` вместо
  `logTelegram`, рендер `/graphs` по команде из `aw/cmd`.
- `platformio.ini`: убрать gyverlibs/Settings, DEVFULL, UniversalTelegramBot.
**Критерий:** логи ESP в Grafana; `/water` из Telegram доходит до полива;
`/graphs` возвращает ASCII; `aw/online` переключается при ребуте ESP;
OTA и UART-обмен с Mega не регрессировали.

### Этап 4. Дашборд «Автополив» в Grafana
Панель логов `{stack="watering"}` + статус `aw/online`.
**Критерий:** отладка полива не требует ничего, кроме Grafana и Telegram.

## Идеи (сознательно не в скоупе)

- PNG-графики из Grafana (grafana-image-renderer) вместо ASCII в Telegram.
- Логи Mega по UART → ESP → Loki (единый поток обоих контроллеров).
- Метрики влажности из логов → полноценные панели (шаг после этапа 4).
- Алиса: mosquitto-bridge в Dealgate ИЛИ навык «Маша» (ТЗ Джарвиса) — отдельное решение.
- Единый лог Mega+ESP, отказ от SD.

## Статус реализации (актуализировано 2026-07-21)

Все этапы 1–4 реализованы и задеплоены (тикеты #12–#18 + внутреннее ревью и
ревью GLM с фиксами). Отклонения от плана, принятые по ходу:

- **ASCII-графики выпилены целиком** (issue #20, пересмотр микро-решения 2):
  Graph/PointsHoler/`esp_graphs`/`/graphs` удалены; метрики (t, h, ram,
  влажность почвы по растениям) aw-server извлекает из живого aw/state и пушит
  в Loki (esp-state/esp-plant), панели — в дашборде «Автополив». Идея
  «метрики парсингом из логов LogQL» реализована именно так, отдельного
  пайплайна метрик по-прежнему нет.
- **aw/state — passthrough**: ESP публикует кадр Mega как есть, без
  deserializeState/serializeState (лишние поля "c"/"timestamp" сервер игнорирует).
- **Часовая сводка отключена** (HOURLY_SUMMARY_INTERVAL_S=0, решение
  2026-07-21); код поддерживает любой интервал.
- **Уведомления о переходах aw/online в чат** — замена старого
  «Esp started successfully», плюс офлайн-сторона через LWT.
- Микро-решение 1 (сводки планирует бот) — реализовано, но сводка выключена;
  /state отвечает из retained с учётом неизвестного возраста после рестарта.

2026-07-21: прошивка залита и проверена на железе владельцем — тикеты
#13/#15/#16/#17/#20 и зонтик #11 закрыты, миграция завершена.
Не закрыто: ACL брокера (#19), имена растений в PLANT_NAMES (стек watering).
