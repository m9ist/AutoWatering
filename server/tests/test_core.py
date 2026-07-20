"""Ядро-роутер возвращает данные (LokiPush), а не вызывает порт напрямую — тест
работает без фейков/моков и без сети (issue #14, критерий приёмки)."""

from aw_server.core import LokiPush, Router


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
