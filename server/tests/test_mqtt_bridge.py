"""MqttBridge: диспетчеризация входящего сообщения по топику (log/event/state) на
правильный порт (issue #15). Без сети и без живого брокера — MqttBridge() создаёт
paho.mqtt.client.Client в памяти (без .start()/.connect()), _on_message() вызывается
напрямую с фейковым сообщением; бизнес-логика уже покрыта tests/test_core.py, здесь
проверяется только маршрутизация."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone

from aw_server.config import Config
from aw_server.core import Router
from aw_server.mqtt_bridge import MqttBridge


@dataclass
class _FakeMsg:
    topic: str
    payload: bytes


class _RecordingLokiPort:
    def __init__(self) -> None:
        self.pushed: list = []

    def push(self, effect) -> None:
        self.pushed.append(effect)


class _RecordingTelegramPort:
    def __init__(self) -> None:
        self.broadcasted: list[str] = []

    def broadcast(self, text: str) -> None:
        self.broadcasted.append(text)


def _cfg() -> Config:
    return Config(
        mqtt_host="mosquitto",
        mqtt_port=1883,
        mqtt_username="aw-server",
        mqtt_password="secret",
        mqtt_topic_prefix="aw/",
        loki_url="http://loki:3100",
        log_level="INFO",
        telegram_bot_token="123:test",
        telegram_whitelist_chat_ids=frozenset({"-1"}),
        state_freshness_hours=2,
        hourly_summary_interval_s=3600,
    )


def _bridge():
    cfg = _cfg()
    router = Router(log_topic_prefix=cfg.mqtt_topic_prefix + "log/", whitelist_chat_ids=cfg.telegram_whitelist_chat_ids)
    loki_port = _RecordingLokiPort()
    telegram_port = _RecordingTelegramPort()
    bridge = MqttBridge(cfg, router, loki_port, telegram_port)
    return bridge, router, loki_port, telegram_port


def test_log_topic_is_routed_to_loki():
    bridge, _router, loki_port, telegram_port = _bridge()

    bridge._on_message(None, None, _FakeMsg("aw/log/esp", b'{"msg": "boot"}'))

    assert len(loki_port.pushed) == 1
    assert telegram_port.broadcasted == []


def test_event_topic_is_routed_to_telegram_broadcast():
    bridge, _router, loki_port, telegram_port = _bridge()

    bridge._on_message(None, None, _FakeMsg("aw/event", b'{"type": "confirm", "text": "hello"}'))

    assert telegram_port.broadcasted == ["hello"]
    assert loki_port.pushed == []


def test_state_topic_is_stored_in_router_and_not_broadcast():
    bridge, router, loki_port, telegram_port = _bridge()

    bridge._on_message(None, None, _FakeMsg("aw/state", b'{"t": 200}'))

    assert telegram_port.broadcasted == []
    assert loki_port.pushed == []
    assert router.hourly_summary_text(datetime.now(timezone.utc)) is not None


def test_broken_message_on_any_topic_does_not_raise():
    bridge, _router, _loki_port, _telegram_port = _bridge()

    # payload, который ломает JSON — не должно всплыть исключение из _on_message
    # (иначе один плохой пакет валит поток paho, см. mqtt_bridge._on_message)
    bridge._on_message(None, None, _FakeMsg("aw/event", b"\xff\xfe not json"))
    bridge._on_message(None, None, _FakeMsg("aw/state", b"\xff\xfe not json"))
    bridge._on_message(None, None, _FakeMsg("aw/log/esp", b"\xff\xfe not json"))
