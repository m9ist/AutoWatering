"""Транспортный адаптер: paho-mqtt <-> Router <-> LokiPort/TelegramBroadcastPort.

Тонкая обвязка без бизнес-логики — она в core.Router (см. tests/test_core.py).
Один MQTT-клиент обслуживает обе роли aw-server: подписка aw/log/# (issue #14,
пуш в Loki) и aw/event + aw/state (issue #15, телеграм-бот). Живой брокер здесь
не тестируется юнит-тестами: устойчивость (reconnect) проверяется по критериям
тикетов на деплое; ветвление по топику — в tests/test_mqtt_bridge.py на фейковых
сообщениях, без сети.
"""

from __future__ import annotations

import json
import logging
import time
from datetime import datetime, timezone
from typing import Protocol

import paho.mqtt.client as mqtt

from .config import Config
from .core import Router
from .loki import HttpLokiPort

log = logging.getLogger(__name__)


class TelegramBroadcastPort(Protocol):
    """Интерфейс порта рассылки в whitelist-чаты (реализация — telegram_bot.PtbTelegramPort).

    broadcast() вызывается из потока paho — реализация обязана быть thread-safe.
    """

    def broadcast(self, text: str) -> None: ...


class MqttCmdPort:
    """Порт публикации Команды в aw/cmd (используется обработчиками telegram-команд)."""

    def __init__(self, client: mqtt.Client, topic: str) -> None:
        self._client = client
        self._topic = topic

    def publish(self, payload: dict) -> None:
        self._client.publish(self._topic, json.dumps(payload))


class MqttBridge:
    def __init__(
        self,
        cfg: Config,
        router: Router,
        loki_port: HttpLokiPort,
        telegram_port: TelegramBroadcastPort,
    ) -> None:
        self._cfg = cfg
        self._router = router
        self._loki_port = loki_port
        self._telegram_port = telegram_port
        self._log_topic = cfg.mqtt_topic_prefix + "log/#"
        self._event_topic = cfg.mqtt_topic_prefix + "event"
        self._state_topic = cfg.mqtt_topic_prefix + "state"
        self.cmd_topic = cfg.mqtt_topic_prefix + "cmd"

        self._client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id="aw-server",
        )
        self._client.username_pw_set(cfg.mqtt_username, cfg.mqtt_password)
        # backoff переподключения: брокер лежит недолго (рестарт стека mqtt) — не долбим его
        self._client.reconnect_delay_set(min_delay=1, max_delay=120)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

    @property
    def client(self) -> mqtt.Client:
        """Доступ для MqttCmdPort — публикует в aw/cmd тот же клиент, что подписан на входящие."""
        return self._client

    def start(self) -> None:
        # connect_async + loop_start: если брокер недоступен при старте, paho сам
        # ретраит подключение в фоновом потоке по reconnect_delay_set — сервис не падает
        self._client.connect_async(self._cfg.mqtt_host, self._cfg.mqtt_port)
        self._client.loop_start()

    def _on_connect(self, client, userdata, flags, reason_code, properties=None) -> None:
        if reason_code != 0:
            log.error(
                "не удалось подключиться к брокеру %s:%s: %s",
                self._cfg.mqtt_host, self._cfg.mqtt_port, reason_code,
            )
            return
        client.subscribe(self._log_topic)
        client.subscribe(self._event_topic)
        client.subscribe(self._state_topic)
        log.info(
            "подключены к брокеру %s:%s, подписаны на %s, %s, %s",
            self._cfg.mqtt_host, self._cfg.mqtt_port,
            self._log_topic, self._event_topic, self._state_topic,
        )

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties=None) -> None:
        log.warning(
            "отключились от брокера %s:%s (%s), paho переподключится сам",
            self._cfg.mqtt_host, self._cfg.mqtt_port, reason_code,
        )

    def _on_message(self, client, userdata, msg) -> None:
        try:
            if msg.topic == self._event_topic:
                for effect in self._router.handle_event_message(msg.payload):
                    self._telegram_port.broadcast(effect.text)
                return

            if msg.topic == self._state_topic:
                result = self._router.handle_state_message(msg.payload, datetime.now(timezone.utc))
                if not result.ok:
                    log.warning(
                        "не удалось разобрать aw/state (%d байт): %s", len(msg.payload), result.reason
                    )
                return

            received_at_ns = time.time_ns()
            effects = self._router.handle_message(msg.topic, msg.payload, received_at_ns)
            for effect in effects:
                self._loki_port.push(effect)
        except Exception:
            # исключение из колбэка paho молча теряется в сетевом потоке — логируем сами
            # с полным stacktrace и не даём одному плохому сообщению уронить обработку следующих
            log.exception("ошибка обработки сообщения %s (%d байт)", msg.topic, len(msg.payload))
