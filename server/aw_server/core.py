"""Ядро-роутер aw-server: чистые функции/классы, без транспортных зависимостей.

Роутер не знает про paho-mqtt/requests/python-telegram-bot — только про данные.
Транспортные адаптеры (aw_server.mqtt_bridge, aw_server.loki, aw_server.telegram_bot)
вызывают Router и применяют возвращённые эффекты к реальным портам. Это даёт
pytest-шов: ядро тестируется без живого брокера, без сети и без Telegram API
(см. tests/test_core.py).

Роли (все — методы одного Router, как и было задумано в issue #14):
- мост Лог-потока: aw/log/<service> -> эффект пуша строки в Loki (issue #14);
- телеграм-бот: команды -> Команда в aw/cmd + ответ пользователю, aw/event ->
  сообщение в whitelist-чаты, aw/state (retained) -> кэш для /state и часовой
  сводки (issue #15).

Протокол Команд и границы значений — по src/State.h и src/CommandParser.h этой же
репы (прошивка ESP/Mega). aw-server не подключает C++-заголовки напрямую, поэтому
ключи команд и границы продублированы ниже как константы: если они изменятся в
прошивке, обновить и здесь (шов #17 — контракт передаётся байт в байт, не
переформатируется).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from datetime import datetime, timedelta

STACK_LABEL = "watering"

# --- src/State.h ---
COMMAND_KEY = "c"
CMD_WATER = "esp_water"
CMD_CONFIG = "esp_plant_conf"
CMD_DAILY = "esp_daily"
CMD_CHECK_VALVES = "esp_check_valves"
# Команда графиков не существовала в старом коде (ASCII-графики раньше рисовались
# и отправлялись прямо с ESP по таймеру/команде из телеграма). В новой схеме
# "/graphs" едет как Команда в aw/cmd, а ESP отвечает через aw/event — имя команды
# для этого контракта здесь и вводится (см. issue #17, где ESP должен его принять).
CMD_GRAPHS = "esp_graphs"

PLANTS_AMOUNT = 16
MAX_WATER_AMOUNT_ML = 200
_MAX_NUMBER_DIGITS = 6  # защита от переполнения int при парсинге (см. CommandParser.h)


@dataclass(frozen=True)
class LokiPush:
    """Эффект: строка лога, готовая к пушу в Loki push API.

    labels — метки потока (низкая кардинальность: stack + service).
    metadata — структурированные метаданные записи (level и т.п.), не метка потока.
    timestamp_ns — строка с наносекундами; ставится на приёме, а не из тела сообщения.
    """

    labels: dict[str, str]
    timestamp_ns: str
    line: str
    metadata: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class TelegramBroadcast:
    """Эффект: текст, который нужно отправить во все whitelist-чаты (не ответ на команду)."""

    text: str


@dataclass(frozen=True)
class CommandResult:
    """Результат обработки telegram-команды.

    reply_text — что ответить в чат, откуда пришла команда (None — не отвечать).
    cmd_payload — готовый JSON Команды для публикации в aw/cmd, если команда его порождает.
    ignored — True для сообщений из чужого (не whitelist) чата: адаптер не отвечает,
    только логирует источник (issue #15, критерий "чужие чаты игнорируются").
    """

    reply_text: str | None
    cmd_payload: dict | None = None
    ignored: bool = False


@dataclass(frozen=True)
class StateStored:
    """Результат приёма aw/state. ok=False — не удалось разобрать, адаптер логирует reason."""

    ok: bool
    reason: str | None = None


@dataclass
class _StateSnapshot:
    raw: dict
    # None — стейт пришёл как retained при (ре)подписке: брокер отдаёт
    # последнее сохранённое сообщение заново, момент его реальной публикации
    # ESP неизвестен. Такой стейт показываем в /state с оговоркой, но не
    # считаем свежим (часовая сводка молчит), пока ESP не пришлёт живой.
    received_at: datetime | None


ONLINE_SERVICE = "esp-online"


_HELP_TEXT = (
    "/water plantX Yml — полить растение X объёмом Y мл (пример: /water plant3 50ml)\n"
    "/config plantX Yml — задать дневную норму растения X (пример: /config plant2 20ml)\n"
    "/daily — дневной полив всех растений по нормам\n"
    "/checkvalves — проверить, что клапаны физически подключены\n"
    "/state — последний известный стейт системы (из retained aw/state)\n"
    "/graphs — ASCII-графики влажности по растениям\n"
    "/help — это сообщение"
)

_PLANT_STATUS = {
    10: "on",
    1: "off (выключено пользователем)",
    2: "off (авария)",
    -1: "не задано",
}


class Router:
    """Входящее MQTT-сообщение или telegram-команда -> эффекты (данные, не I/O)."""

    def __init__(
        self,
        log_topic_prefix: str = "aw/log/",
        whitelist_chat_ids: frozenset[str] = frozenset(),
        state_freshness: timedelta = timedelta(hours=2),
    ) -> None:
        self._log_topic_prefix = log_topic_prefix
        self._whitelist = whitelist_chat_ids
        self._state_freshness = state_freshness
        self._state: _StateSnapshot | None = None
        self._online: str | None = None

    # ---------------------------------------------------------------- issue #14
    def handle_message(self, topic: str, payload: bytes, received_at_ns: int) -> list[LokiPush]:
        if not topic.startswith(self._log_topic_prefix):
            return []  # не лог-топик — обрабатывается отдельными методами ниже (issue #15)

        service = topic[len(self._log_topic_prefix):] or "unknown"
        line, level = _parse_log_payload(payload)
        labels = {"stack": STACK_LABEL, "service": service}
        metadata = {"level": level} if level else {}
        return [LokiPush(labels=labels, timestamp_ns=str(received_at_ns), line=line, metadata=metadata)]

    # ---------------------------------------------------------------- issue #18
    def handle_online_message(self, payload: bytes, received_at_ns: int) -> list[LokiPush]:
        """aw/online (retained LWT ESP, issue #16): "1"/"0" -> лог-строка перехода в Loki.

        Логируем только смену состояния, не каждую доставку retained-сообщения —
        иначе ресабскрайб aw-server (реконнект к брокеру) плодил бы дубли той же
        строки без реального перехода ESP. Первое полученное значение (self._online
        ещё None) тоже логируется — это текущий статус на момент старта aw-server.
        "online" в structured metadata — не метка потока (сохраняем кардинальность
        {stack, service} низкой), но доступен LogQL `| unwrap` для панели статуса.
        """
        try:
            value = payload.decode("ascii").strip()
        except UnicodeDecodeError:
            # parse-with-default: не-ASCII в aw/online — мусор, а не переход;
            # молча игнорируем, как и любые значения кроме "0"/"1" ниже
            return []
        if value not in ("0", "1"):
            return []
        if value == self._online:
            return []
        self._online = value

        is_online = value == "1"
        labels = {"stack": STACK_LABEL, "service": ONLINE_SERVICE}
        metadata = {"level": "info" if is_online else "warning", "online": value}
        line = "ESP online" if is_online else "ESP offline"
        return [LokiPush(labels=labels, timestamp_ns=str(received_at_ns), line=line, metadata=metadata)]

    # ---------------------------------------------------------------- issue #15: whitelist
    def is_allowed(self, chat_id: str) -> bool:
        return chat_id in self._whitelist

    # ---------------------------------------------------------------- issue #15: aw/event -> чат
    def handle_event_message(self, payload: bytes) -> list[TelegramBroadcast]:
        """{type, text} -> рассылка text во все whitelist-чаты. Битый/неполный JSON или
        отсутствие text — молча игнорируем (не роняем обработку, не шлём мусор в чат)."""
        try:
            data = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return []
        if not isinstance(data, dict):
            return []
        text = data.get("text")
        if not text:
            return []
        return [TelegramBroadcast(text=str(text))]

    # ---------------------------------------------------------------- issue #15: aw/state (retained)
    def handle_state_message(
        self, payload: bytes, received_at: datetime, retained: bool = False
    ) -> StateStored:
        """retained=True — доставка из retained-хранилища брокера при (ре)подписке,
        а не свежая публикация ESP: возраст неизвестен, свежим не считаем (иначе
        реконнект aw-server «омолаживал» бы стейт мёртвого ESP на state_freshness)."""
        if not payload:
            # MQTT-идиома очистки retained-сообщения — пустой payload, не ошибка формата
            # (см. README стека watering, "Проверка": mosquitto_pub -r -n). Считаем, что
            # состояния больше нет, а не пытаемся распарсить пустую строку как JSON.
            self._state = None
            return StateStored(ok=True)
        try:
            data = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            return StateStored(ok=False, reason=str(exc))
        if not isinstance(data, dict):
            return StateStored(ok=False, reason="payload — не JSON-объект")
        if retained:
            # Брокер переигрывает retained и при кратких реконнектах paho, не только
            # при рестарте aw-server. Если пейлоад совпадает с кэшем — это тот же
            # стейт, чей живой возраст мы уже знаем, не затираем его. Отличается —
            # стейт публиковался, пока нас не было: данные берём, возраст честно
            # неизвестен (ревью GLM).
            if self._state is not None and self._state.raw == data:
                return StateStored(ok=True)
            self._state = _StateSnapshot(raw=data, received_at=None)
            return StateStored(ok=True)
        self._state = _StateSnapshot(raw=data, received_at=received_at)
        return StateStored(ok=True)

    # ---------------------------------------------------------------- issue #15: команды
    def handle_help(self, chat_id: str) -> CommandResult:
        if not self.is_allowed(chat_id):
            return CommandResult(reply_text=None, ignored=True)
        return CommandResult(reply_text=_HELP_TEXT)

    def handle_water(self, chat_id: str, args: list[str], now: datetime) -> CommandResult:
        return self._plant_command(chat_id, CMD_WATER, "/water", args, now)

    def handle_config(self, chat_id: str, args: list[str], now: datetime) -> CommandResult:
        return self._plant_command(chat_id, CMD_CONFIG, "/config", args, now)

    def handle_daily(self, chat_id: str, now: datetime) -> CommandResult:
        return self._simple_command(chat_id, CMD_DAILY, "Отправлена команда дневного полива.", now)

    def handle_checkvalves(self, chat_id: str, now: datetime) -> CommandResult:
        return self._simple_command(
            chat_id, CMD_CHECK_VALVES, "Отправлена команда проверки клапанов.", now
        )

    def handle_graphs(self, chat_id: str, now: datetime) -> CommandResult:
        return self._simple_command(
            chat_id, CMD_GRAPHS, "Запрошены графики, жду ответ от ESP.", now
        )

    def handle_state(self, chat_id: str, now: datetime) -> CommandResult:
        if not self.is_allowed(chat_id):
            return CommandResult(reply_text=None, ignored=True)
        return CommandResult(reply_text=self._state_text(now))

    # ---------------------------------------------------------------- issue #15: планировщик бота
    def hourly_summary_text(self, now: datetime) -> str | None:
        """None — сводку слать не нужно (стейта нет, он из retained с неизвестным
        возрастом, или старше state_freshness)."""
        if self._state is None or self._state.received_at is None:
            return None
        age = now - self._state.received_at
        if age > self._state_freshness:
            return None
        return "Часовая сводка:\n" + _render_state(self._state.raw, age)

    # ---------------------------------------------------------------- internal
    def _plant_command(
        self, chat_id: str, esp_command: str, prefix: str, args: list[str], now: datetime
    ) -> CommandResult:
        if not self.is_allowed(chat_id):
            return CommandResult(reply_text=None, ignored=True)

        message = f"{prefix} {' '.join(args)}".rstrip()
        parsed = _parse_plant_amount_command(message, prefix)
        if parsed is None:
            return CommandResult(
                reply_text=f"Неверный формат команды. Пример: {prefix} plant3 50ml"
            )

        plant_id, amount = parsed
        # Границы значений (id растения, объём) — как на ESP/Mega (src/CommandParser.h,
        # src/State.h): без этой проверки на сервере пользователь узнал бы об отказе
        # только после round-trip до прошивки (или не узнал бы вовсе, если ESP молча
        # отбросит Команду).
        if not (0 <= plant_id < PLANTS_AMOUNT and 0 <= amount <= MAX_WATER_AMOUNT_ML):
            return CommandResult(
                reply_text=(
                    f"Отказ: id растения 0..{PLANTS_AMOUNT - 1}, "
                    f"объём 0..{MAX_WATER_AMOUNT_ML}мл"
                )
            )

        payload = {
            COMMAND_KEY: esp_command,
            "timestamp": _timestamp(now),
            "plantId": plant_id,
            "amountMl": amount,
        }
        return CommandResult(
            reply_text=self._ack_text(f"Команда отправлена: plant{plant_id}, {amount}мл"),
            cmd_payload=payload,
        )

    def _simple_command(self, chat_id: str, esp_command: str, ack_text: str, now: datetime) -> CommandResult:
        if not self.is_allowed(chat_id):
            return CommandResult(reply_text=None, ignored=True)
        payload = {COMMAND_KEY: esp_command, "timestamp": _timestamp(now)}
        return CommandResult(reply_text=self._ack_text(ack_text), cmd_payload=payload)

    def _ack_text(self, ack: str) -> str:
        """ESP подключён clean-session с QoS0 — Команду, опубликованную при офлайн-ESP,
        брокер просто выбрасывает. Подтверждать её без оговорки — ложный ACK (ревью GLM);
        статус берём из retained aw/online (LWT прошивки)."""
        if self._online == "0":
            return ack + "\n⚠️ ESP офлайн (aw/online=0) — команда до него не дойдёт."
        return ack

    def _state_text(self, now: datetime) -> str:
        if self._state is None:
            return "Стейта нет: с ESP ещё не пришёл ни один aw/state."
        if self._state.received_at is None:
            return (
                "Стейт из retained-хранилища брокера, возраст неизвестен "
                "(aw-server перезапускался; свежий придёт с очередным стейтом ESP):\n"
                + _render_state(self._state.raw, None)
            )
        age = now - self._state.received_at
        if age > self._state_freshness:
            minutes = int(age.total_seconds() // 60)
            return (
                f"Стейт устарел: последний пришёл {minutes} мин назад "
                f"(лимит свежести — {self._state_freshness})."
            )
        return _render_state(self._state.raw, age)


def _parse_log_payload(payload: bytes) -> tuple[str, str | None]:
    """Разбирает {ts, lvl, msg}. Битый/неполный JSON не роняет обработку — уходит raw-строкой,
    без уровня в structured metadata. ts из тела не трогаем — его ставит ESP, наш timestamp
    для Loki отдельный (см. Router.handle_message).
    """

    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError:
        return repr(payload), None

    try:
        data = json.loads(text)
    except (json.JSONDecodeError, ValueError):
        return text, None

    if not isinstance(data, dict) or "msg" not in data:
        return text, None

    level = data.get("lvl")
    level = str(level) if isinstance(level, (str, int)) else None
    return text, level


def _is_valid_integer(s: str) -> bool:
    return len(s) > 0 and s.isascii() and s.isdigit()


def _parse_plant_amount_command(message: str, prefix: str) -> tuple[int, int] | None:
    """Порт src/CommandParser.h::parsePlantAmountCommand на Python.

    Разбирает "<prefix> plantX Yml", например "/water plant3 50ml". Только формат;
    границы значений проверяет вызывающая сторона (см. _plant_command). Длина числа
    ограничена _MAX_NUMBER_DIGITS цифрами — защита от переполнения int (как на ESP).
    """

    expected_start = f"{prefix} plant"
    if not message.startswith(expected_start):
        return None
    if not message.endswith("ml"):
        return None

    space_pos = message.find(" ", len(expected_start))
    if space_pos < 0:
        return None

    plant_id_str = message[len(expected_start):space_pos]
    amount_str = message[space_pos + 1: len(message) - 2]
    if not (_is_valid_integer(plant_id_str) and _is_valid_integer(amount_str)):
        return None
    if len(plant_id_str) > _MAX_NUMBER_DIGITS or len(amount_str) > _MAX_NUMBER_DIGITS:
        return None

    return int(plant_id_str), int(amount_str)


def _timestamp(now: datetime) -> str:
    return now.strftime("%Y-%m-%d %H:%M:%S")


def _render_state(raw: dict, age: timedelta | None) -> str:
    """Человекочитаемый рендер UART-стейта (src/State.h::serializeState). Сырые ключи
    (t, h, ram, p[].{id,on,or,m}) читаются с .get() — частично битый/неполный JSON
    не должен ронять /state или часовую сводку (MVP: остальное как есть, без валидации).
    age=None — возраст неизвестен (стейт из retained, см. handle_state_message)."""

    if age is None:
        lines = []
    else:
        minutes = int(age.total_seconds() // 60)
        lines = [f"(получен {minutes} мин назад)"]

    t = raw.get("t")
    if isinstance(t, (int, float)):
        lines.append(f"Температура: {t / 10:.1f}°C")
    h = raw.get("h")
    if isinstance(h, (int, float)):
        lines.append(f"Влажность воздуха: {h / 10:.1f}%")
    ram = raw.get("ram")
    if ram is not None:
        lines.append(f"RAM: {ram}")

    plants = raw.get("p")
    if isinstance(plants, list) and plants:
        lines.append("Растения:")
        for p in plants:
            if not isinstance(p, dict):
                continue
            status = _PLANT_STATUS.get(p.get("on"), f"?{p.get('on')}")
            lines.append(
                f"  plant{p.get('id')}: {status}, влага={p.get('or')}, норма={p.get('m')}мл"
            )

    return "\n".join(lines)
