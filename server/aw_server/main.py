"""Точка входа aw-server (роль #14: мост Лог-потока aw/log/# -> Loki)."""

from __future__ import annotations

import logging
import time

from . import config
from .core import Router
from .loki import HttpLokiPort
from .mqtt_bridge import MqttBridge

log = logging.getLogger("aw_server")

HEARTBEAT_PATH = "/tmp/healthy"
HEARTBEAT_INTERVAL_S = 10


def _touch_heartbeat() -> None:
    # читается HEALTHCHECK контейнера: показывает, что главный поток жив, а не что
    # брокер/Loki доступны — недоступность брокера/Loki не должна валить контейнер (#14)
    with open(HEARTBEAT_PATH, "w") as f:
        f.write(str(time.time()))


def main() -> None:
    cfg = config.load()
    logging.basicConfig(level=cfg.log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    loki_port = HttpLokiPort(cfg.loki_url)
    router = Router(log_topic_prefix=cfg.mqtt_topic_prefix + "log/")
    bridge = MqttBridge(cfg, router, loki_port)
    bridge.start()

    log.info("aw-server запущен (log-bridge), топик %slog/#", cfg.mqtt_topic_prefix)
    while True:
        _touch_heartbeat()
        time.sleep(HEARTBEAT_INTERVAL_S)


if __name__ == "__main__":
    main()
