"""Конфигурация из окружения: дефолты и обязательность переменных (issue #14)."""

import pytest

from aw_server import config


def _set_required_env(monkeypatch):
    monkeypatch.setenv("MQTT_HOST", "mosquitto")
    monkeypatch.setenv("MQTT_USERNAME", "aw-server")
    monkeypatch.setenv("MQTT_PASSWORD", "secret")
    monkeypatch.setenv("LOKI_URL", "http://loki:3100")


def test_load_applies_defaults(monkeypatch):
    _set_required_env(monkeypatch)
    monkeypatch.delenv("MQTT_PORT", raising=False)
    monkeypatch.delenv("MQTT_TOPIC_PREFIX", raising=False)
    monkeypatch.delenv("LOG_LEVEL", raising=False)

    cfg = config.load()

    assert cfg.mqtt_port == 1883
    assert cfg.mqtt_topic_prefix == "aw/"
    assert cfg.log_level == "INFO"


def test_load_missing_required_var_raises(monkeypatch):
    _set_required_env(monkeypatch)
    monkeypatch.delenv("MQTT_HOST", raising=False)

    with pytest.raises(RuntimeError):
        config.load()
