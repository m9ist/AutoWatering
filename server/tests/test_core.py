"""Ядро-роутер возвращает данные (LokiPush), а не вызывает порт напрямую — тест
работает без фейков/моков и без сети (issue #14, критерий приёмки)."""

from datetime import datetime, timedelta, timezone

from aw_server.core import CommandResult, LokiPush, Router, TelegramBroadcast


def test_log_message_becomes_loki_push_with_level_metadata():
    router = Router()
    payload = b'{"ts": 123456, "lvl": "info", "msg": "boot"}'

    effects = router.handle_message("aw/log/esp", payload, received_at_ns=1_700_000_000_000_000_000)

    assert effects == [
        LokiPush(
            labels={"stack": "watering", "service": "esp"},
            timestamp_ns="1700000000000000000",
            line=payload.decode("utf-8"),
            metadata={"level": "info"},
        )
    ]


def test_broken_json_becomes_raw_line_without_level():
    router = Router()
    payload = b"not json at all"

    effects = router.handle_message("aw/log/esp", payload, received_at_ns=42)

    assert effects == [
        LokiPush(
            labels={"stack": "watering", "service": "esp"},
            timestamp_ns="42",
            line="not json at all",
            metadata={},
        )
    ]


def test_incomplete_json_without_msg_becomes_raw_line():
    router = Router()
    payload = b'{"ts": 1, "lvl": "warn"}'

    effects = router.handle_message("aw/log/esp", payload, received_at_ns=1)

    assert effects[0].line == payload.decode("utf-8")
    assert effects[0].metadata == {}


def test_undecodable_payload_does_not_raise():
    router = Router()
    payload = b"\xff\xfe\x00broken"

    effects = router.handle_message("aw/log/esp", payload, received_at_ns=1)

    assert effects[0].metadata == {}
    assert effects[0].line  # repr() непустой


def test_other_topics_are_ignored():
    router = Router()

    effects = router.handle_message("aw/state", b"{}", received_at_ns=1)

    assert effects == []


def test_service_is_taken_from_topic_suffix():
    router = Router()

    effects = router.handle_message("aw/log/mega", b'{"msg": "x"}', received_at_ns=1)

    assert effects[0].labels["service"] == "mega"


# --------------------------------------------------------------------------- issue #15
# Команды telegram -> aw/cmd, whitelist, aw/event -> чат, /state и часовая сводка
# из retained aw/state. Тест кормит ядро входящим сообщением/командой и проверяет
# только исходящие эффекты (CommandResult/TelegramBroadcast) — без фейков портов,
# без сети, без Telegram API (см. tasks/2026-07-20_mqtt_server_migration.md).

ALLOWED_CHAT = "-1001"
FOREIGN_CHAT = "-999"


def _router(**kwargs) -> Router:
    kwargs.setdefault("whitelist_chat_ids", frozenset({ALLOWED_CHAT}))
    return Router(**kwargs)


def test_water_valid_command_publishes_cmd_and_replies():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_water(ALLOWED_CHAT, ["plant2", "50ml"], now)

    assert result.ignored is False
    assert result.cmd_payload == {
        "c": "esp_water",
        "timestamp": "2026-07-20 12:00:00",
        "plantId": 2,
        "amountMl": 50,
    }
    assert result.reply_text is not None


def test_config_valid_command_publishes_cmd():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_config(ALLOWED_CHAT, ["plant5", "30ml"], now)

    assert result.cmd_payload == {
        "c": "esp_plant_conf",
        "timestamp": "2026-07-20 12:00:00",
        "plantId": 5,
        "amountMl": 30,
    }


def test_water_out_of_bounds_plant_id_is_rejected_without_publishing():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_water(ALLOWED_CHAT, ["plant99", "10ml"], now)

    assert result.cmd_payload is None
    assert result.ignored is False
    assert "0..15" in result.reply_text


def test_water_out_of_bounds_amount_is_rejected_without_publishing():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_water(ALLOWED_CHAT, ["plant2", "999ml"], now)

    assert result.cmd_payload is None
    assert "0..200" in result.reply_text


def test_water_malformed_format_is_rejected_without_publishing():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    for bad_args in (["plant2"], ["plantX", "50ml"], ["plant2", "50"], []):
        result = router.handle_water(ALLOWED_CHAT, bad_args, now)
        assert result.cmd_payload is None
        assert result.reply_text is not None


def test_command_from_foreign_chat_is_ignored():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_water(FOREIGN_CHAT, ["plant2", "50ml"], now)

    assert result == CommandResult(reply_text=None, cmd_payload=None, ignored=True)


def test_daily_command_publishes_cmd_without_plant_args():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_daily(ALLOWED_CHAT, now)

    assert result.cmd_payload == {"c": "esp_daily", "timestamp": "2026-07-20 12:00:00"}


def test_checkvalves_command_publishes_cmd():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_checkvalves(ALLOWED_CHAT, now)

    assert result.cmd_payload == {"c": "esp_check_valves", "timestamp": "2026-07-20 12:00:00"}


def test_graphs_command_publishes_cmd():
    router = _router()
    now = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)

    result = router.handle_graphs(ALLOWED_CHAT, now)

    assert result.cmd_payload == {"c": "esp_graphs", "timestamp": "2026-07-20 12:00:00"}


def test_help_command_from_allowed_chat_replies_with_text():
    router = _router()

    result = router.handle_help(ALLOWED_CHAT)

    assert result.cmd_payload is None
    assert "/water" in result.reply_text
    assert "/checkvalves" in result.reply_text


def test_help_command_from_foreign_chat_is_ignored():
    router = _router()

    result = router.handle_help(FOREIGN_CHAT)

    assert result == CommandResult(reply_text=None, cmd_payload=None, ignored=True)


def test_event_message_becomes_broadcast_to_whitelist():
    router = _router()
    payload = b'{"type": "confirm", "text": "Watered plant2, 50ml"}'

    effects = router.handle_event_message(payload)

    assert effects == [TelegramBroadcast(text="Watered plant2, 50ml")]


def test_event_message_without_text_is_ignored():
    router = _router()

    assert router.handle_event_message(b'{"type": "confirm"}') == []
    assert router.handle_event_message(b"not json") == []


def test_state_command_without_any_state_says_so():
    router = _router()

    result = router.handle_state(ALLOWED_CHAT, datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc))

    assert "нет" in result.reply_text.lower()
    assert result.cmd_payload is None


def test_state_command_from_foreign_chat_is_ignored():
    router = _router()

    result = router.handle_state(FOREIGN_CHAT, datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc))

    assert result == CommandResult(reply_text=None, cmd_payload=None, ignored=True)


def test_retained_state_answers_state_command():
    router = _router()
    received_at = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)
    state_payload = (
        b'{"t": 234, "h": 450, "ram": 4200, '
        b'"p": [{"id": 2, "on": 10, "or": 512, "m": 50}]}'
    )

    stored = router.handle_state_message(state_payload, received_at)
    result = router.handle_state(ALLOWED_CHAT, received_at + timedelta(minutes=5))

    assert stored.ok is True
    assert "23.4" in result.reply_text
    assert "45.0" in result.reply_text
    assert "plant2" in result.reply_text
    assert "50" in result.reply_text


def test_empty_state_payload_clears_state_without_warning():
    router = _router()
    received_at = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)
    router.handle_state_message(b'{"t": 234}', received_at)

    stored = router.handle_state_message(b"", received_at + timedelta(minutes=1))

    assert stored.ok is True
    assert stored.reason is None
    result = router.handle_state(ALLOWED_CHAT, received_at + timedelta(minutes=2))
    assert "нет" in result.reply_text.lower()


def test_malformed_state_message_is_reported_without_raising():
    router = _router()

    stored = router.handle_state_message(b"not json at all", datetime.now(timezone.utc))

    assert stored.ok is False
    assert stored.reason


def test_state_command_reports_stale_state():
    router = _router(state_freshness=timedelta(hours=1))
    received_at = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)
    router.handle_state_message(b'{"t": 1}', received_at)

    result = router.handle_state(ALLOWED_CHAT, received_at + timedelta(hours=5))

    assert "устар" in result.reply_text.lower()


def test_hourly_summary_is_none_without_state():
    router = _router()

    assert router.hourly_summary_text(datetime.now(timezone.utc)) is None


def test_hourly_summary_is_none_when_state_is_stale():
    router = _router(state_freshness=timedelta(hours=1))
    received_at = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)
    router.handle_state_message(b'{"t": 1}', received_at)

    assert router.hourly_summary_text(received_at + timedelta(hours=5)) is None


def test_hourly_summary_returns_text_when_state_is_fresh():
    router = _router(state_freshness=timedelta(hours=1))
    received_at = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)
    router.handle_state_message(b'{"t": 234, "h": 450, "ram": 100}', received_at)

    text = router.hourly_summary_text(received_at + timedelta(minutes=10))

    assert text is not None
    assert "23.4" in text


# --------------------------------------------------------------------------- issue #18
# aw/online (retained LWT ESP, issue #16) -> лог-строка перехода в Loki.


def test_online_first_value_becomes_loki_push():
    router = Router()

    effects = router.handle_online_message(b"1", received_at_ns=1)

    assert effects == [
        LokiPush(
            labels={"stack": "watering", "service": "esp-online"},
            timestamp_ns="1",
            line="ESP online",
            metadata={"level": "info", "online": "1"},
        )
    ]


def test_online_offline_value_has_warning_level():
    router = Router()

    effects = router.handle_online_message(b"0", received_at_ns=1)

    assert effects == [
        LokiPush(
            labels={"stack": "watering", "service": "esp-online"},
            timestamp_ns="1",
            line="ESP offline",
            metadata={"level": "warning", "online": "0"},
        )
    ]


def test_online_repeated_same_value_is_not_logged_again():
    router = Router()
    router.handle_online_message(b"1", received_at_ns=1)

    effects = router.handle_online_message(b"1", received_at_ns=2)

    assert effects == []


def test_online_transition_is_logged():
    router = Router()
    router.handle_online_message(b"1", received_at_ns=1)

    effects = router.handle_online_message(b"0", received_at_ns=2)

    assert effects and effects[0].line == "ESP offline"


def test_online_malformed_payload_is_ignored():
    router = Router()

    assert router.handle_online_message(b"garbage", received_at_ns=1) == []
    assert router.handle_online_message(b"\xff\xfe", received_at_ns=1) == []


# --------------------------------------------------------------------------- фиксы по ревью GLM


def test_command_ack_warns_when_esp_offline():
    # clean-session + QoS0: Команда при офлайн-ESP выбрасывается брокером —
    # ACK без оговорки был бы ложным (ревью GLM, finding 1)
    router = _router()
    router.handle_online_message(b"0", received_at_ns=1)

    result = router.handle_water(ALLOWED_CHAT, ["plant2", "50ml"], datetime.now(timezone.utc))

    assert result.cmd_payload is not None
    assert "офлайн" in result.reply_text


def test_command_ack_has_no_warning_when_esp_online():
    router = _router()
    router.handle_online_message(b"1", received_at_ns=1)

    result = router.handle_daily(ALLOWED_CHAT, datetime.now(timezone.utc))

    assert "офлайн" not in result.reply_text


def test_retained_state_with_same_payload_keeps_known_age():
    # краткий реконнект paho: брокер переигрывает retained с тем же пейлоадом —
    # известный живой возраст стейта не должен затираться (ревью GLM, finding 4)
    router = _router(state_freshness=timedelta(hours=1))
    received_at = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)
    router.handle_state_message(b'{"t": 234}', received_at)

    router.handle_state_message(b'{"t": 234}', received_at + timedelta(minutes=5), retained=True)

    assert router.hourly_summary_text(received_at + timedelta(minutes=10)) is not None


def test_retained_state_with_different_payload_resets_age_to_unknown():
    router = _router(state_freshness=timedelta(hours=1))
    received_at = datetime(2026, 7, 20, 12, 0, 0, tzinfo=timezone.utc)
    router.handle_state_message(b'{"t": 234}', received_at)

    # пейлоад отличается: стейт публиковался, пока aw-server был отключён —
    # данные новее кэша, но их реальный возраст неизвестен
    router.handle_state_message(b'{"t": 250}', received_at + timedelta(minutes=5), retained=True)

    assert router.hourly_summary_text(received_at + timedelta(minutes=10)) is None
    reply = router.handle_state(ALLOWED_CHAT, received_at + timedelta(minutes=10)).reply_text
    assert "250" not in reply or "retained" in reply
