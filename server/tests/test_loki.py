"""HttpLokiPort: корректный запрос к push API и устойчивость к ошибкам Loki (issue #14)."""

import logging

import requests

from aw_server.core import LokiPush
from aw_server.loki import HttpLokiPort


class _FakeResponse:
    def raise_for_status(self) -> None:
        pass


class _RecordingSession:
    def __init__(self, raise_exc: Exception | None = None) -> None:
        self.calls: list[tuple[str, dict, float]] = []
        self._raise_exc = raise_exc

    def post(self, url, json, timeout):
        self.calls.append((url, json, timeout))
        if self._raise_exc is not None:
            raise self._raise_exc
        return _FakeResponse()


def _effect(metadata: dict[str, str]) -> LokiPush:
    return LokiPush(
        labels={"stack": "watering", "service": "esp"},
        timestamp_ns="123",
        line="hello",
        metadata=metadata,
    )


def test_push_sends_expected_url_and_body_with_metadata():
    session = _RecordingSession()
    port = HttpLokiPort("http://loki:3100", session=session)

    port.push(_effect({"level": "info"}))

    assert len(session.calls) == 1
    url, body, _timeout = session.calls[0]
    assert url == "http://loki:3100/loki/api/v1/push"
    assert body == {
        "streams": [
            {
                "stream": {"stack": "watering", "service": "esp"},
                "values": [["123", "hello", {"level": "info"}]],
            }
        ]
    }


def test_push_without_metadata_omits_third_element():
    session = _RecordingSession()
    port = HttpLokiPort("http://loki:3100", session=session)

    port.push(_effect({}))

    _url, body, _timeout = session.calls[0]
    assert body["streams"][0]["values"] == [["123", "hello"]]


def test_base_url_trailing_slash_is_normalized():
    session = _RecordingSession()
    port = HttpLokiPort("http://loki:3100/", session=session)

    port.push(_effect({}))

    url, _body, _timeout = session.calls[0]
    assert url == "http://loki:3100/loki/api/v1/push"


def test_push_swallows_connection_errors_and_logs_full_traceback(caplog):
    session = _RecordingSession(raise_exc=requests.ConnectionError("недоступен"))
    port = HttpLokiPort("http://loki:3100", session=session)

    with caplog.at_level(logging.ERROR, logger="aw_server.loki"):
        port.push(_effect({}))  # не должно бросить исключение

    assert any(record.exc_info for record in caplog.records)
