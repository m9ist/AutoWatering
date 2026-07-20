"""Конфигурация из окружения: дефолты и обязательность переменных (issue #14, #15)."""

import pytest

from aw_server import config


def _set_required_env(monkeypatch):
    monkeypatch.setenv("MQTT_HOST", "mosquitto")
    monkeypatch.setenv("MQTT_USERNAME", "aw-server")
    monkeypatch.setenv("MQTT_PASSWORD", "secret")
    monkeypatch.setenv("LOKI_URL", "http://loki:3100")
    monkeypatch.setenv("TELEGRAM_BOT_TOKEN", "123:test-token")
    monkeypatch.setenv("TELEGRAM_WHITELIST_CHAT_IDS", "-1")


def test_load_applies_defaults(monkeypatch):
    _set_required_env(monkeypatch)
    monkeypatch.delenv("MQTT_PORT", raising=False)
    monkeypatch.delenv("MQTT_TOPIC_PREFIX", raising=False)
    monkeypatch.delenv("LOG_LEVEL", raising=False)
    monkeypatch.delenv("STATE_FRESHNESS_HOURS", raising=False)
    monkeypatch.delenv("HOURLY_SUMMARY_INTERVAL_S", raising=False)

    cfg = config.load()

    assert cfg.mqtt_port == 1883
    assert cfg.mqtt_topic_prefix == "aw/"
    assert cfg.log_level == "INFO"
    assert cfg.state_freshness_hours == 2
    assert cfg.hourly_summary_interval_s == 3600


def test_load_missing_required_var_raises(monkeypatch):
    _set_required_env(monkeypatch)
    monkeypatch.delenv("MQTT_HOST", raising=False)

    with pytest.raises(RuntimeError):
        config.load()


def test_load_missing_telegram_token_raises(monkeypatch):
    _set_required_env(monkeypatch)
    monkeypatch.delenv("TELEGRAM_BOT_TOKEN", raising=False)

    with pytest.raises(RuntimeError):
        config.load()


def test_load_parses_whitelist_with_multiple_chat_ids(monkeypatch):
    _set_required_env(monkeypatch)
    monkeypatch.setenv("TELEGRAM_WHITELIST_CHAT_IDS", "-1, 42 ,  -7")

    cfg = config.load()

    assert cfg.telegram_whitelist_chat_ids == frozenset({"-1", "42", "-7"})
