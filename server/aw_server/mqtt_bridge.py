"""Транспортный адаптер: paho-mqtt <-> Router <-> LokiPort.

Тонкая обвязка без бизнес-логики — она в core.Router (см. tests/test_core.py).
Живой брокер здесь не тестируется юнит-тестами: устойчивость (reconnect) проверяется
по критериям тикета (#14) на деплое.
"""

from __future__ import annotations

import logging
import time

import paho.mqtt.client as mqtt

from .config import Config
from .core import Router
from .loki import HttpLokiPort

log = logging.getLogger(__name__)


class MqttBridge:
    def __init__(self, cfg: Config, router: Router, loki_port: HttpLokiPort) -> None:
        self._cfg = cfg
        self._router = router
        self._loki_port = loki_port
        self._log_topic = cfg.mqtt_topic_prefix + "log/#"

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
        log.info(
            "подключены к брокеру %s:%s, подписаны на %s",
            self._cfg.mqtt_host, self._cfg.mqtt_port, self._log_topic,
        )

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties=None) -> None:
        log.warning(
            "отключились от брокера %s:%s (%s), paho переподключится сам",
            self._cfg.mqtt_host, self._cfg.mqtt_port, reason_code,
        )

    def _on_message(self, client, userdata, msg) -> None:
        received_at_ns = time.time_ns()
        try:
            effects = self._router.handle_message(msg.topic, msg.payload, received_at_ns)
            for effect in effects:
                self._loki_port.push(effect)
        except Exception:
            # исключение из колбэка paho молча теряется в сетевом потоке — логируем сами
            # с полным stacktrace и не даём одному плохому сообщению уронить обработку следующих
            log.exception("ошибка обработки сообщения %s (%d байт)", msg.topic, len(msg.payload))
