"""Ядро-роутер aw-server: чистые функции/классы, без транспортных зависимостей.

Роутер не знает про paho-mqtt/requests — только про данные. Транспортные адаптеры
(aw_server.mqtt_bridge, aw_server.loki) вызывают Router и применяют возвращённые
эффекты к реальным портам. Это даёт pytest-шов: ядро тестируется без живого брокера
и без сети (см. tests/test_core.py).

Единственная роль в этом тикете (issue #14) — мост Лог-потока: aw/log/<service> ->
эффект пуша строки в Loki. Другие роли (телеграм-бот, aw/cmd) — отдельные тикеты
поверх того же Router (issue #11, #15).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field

STACK_LABEL = "watering"


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


class Router:
    """Входящее MQTT-сообщение -> список исходящих эффектов."""

    def __init__(self, log_topic_prefix: str = "aw/log/") -> None:
        self._log_topic_prefix = log_topic_prefix

    def handle_message(self, topic: str, payload: bytes, received_at_ns: int) -> list[LokiPush]:
        if not topic.startswith(self._log_topic_prefix):
            return []  # не лог-топик — вне роли этого тикета (aw/cmd, aw/state и т.п. игнорируем)

        service = topic[len(self._log_topic_prefix):] or "unknown"
        line, level = _parse_log_payload(payload)
        labels = {"stack": STACK_LABEL, "service": service}
        metadata = {"level": level} if level else {}
        return [LokiPush(labels=labels, timestamp_ns=str(received_at_ns), line=line, metadata=metadata)]


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
