"""Конфигурация aw-server из окружения."""

from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    mqtt_host: str
    mqtt_port: int
    mqtt_username: str
    mqtt_password: str
    mqtt_topic_prefix: str
    loki_url: str
    log_level: str


def load() -> Config:
    return Config(
        mqtt_host=_require("MQTT_HOST"),
        mqtt_port=int(os.environ.get("MQTT_PORT", "1883")),
        mqtt_username=_require("MQTT_USERNAME"),
        mqtt_password=_require("MQTT_PASSWORD"),
        mqtt_topic_prefix=os.environ.get("MQTT_TOPIC_PREFIX", "aw/"),
        loki_url=_require("LOKI_URL"),
        log_level=os.environ.get("LOG_LEVEL", "INFO"),
    )


def _require(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"обязательная переменная окружения {name} не задана")
    return value
