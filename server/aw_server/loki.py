"""Порт вывода: HTTP-пуш в Loki push API.

Недоступность Loki не должна ронять сервис (issue #14, критерий устойчивости) —
ошибки сети/HTTP логируются с полным stacktrace (log.exception) и проглатываются;
вызывающая сторона (mqtt_bridge) продолжает обрабатывать следующие сообщения.
"""

from __future__ import annotations

import logging

import requests

from .core import LokiPush

log = logging.getLogger(__name__)


class HttpLokiPort:
    """session инжектируется для теста (без реального HTTP)."""

    def __init__(self, base_url: str, timeout_s: float = 5.0, session: requests.Session | None = None) -> None:
        self._url = base_url.rstrip("/") + "/loki/api/v1/push"
        self._timeout_s = timeout_s
        self._session = session or requests.Session()

    def push(self, effect: LokiPush) -> None:
        entry = [effect.timestamp_ns, effect.line]
        if effect.metadata:
            entry.append(effect.metadata)
        body = {"streams": [{"stream": effect.labels, "values": [entry]}]}
        try:
            response = self._session.post(self._url, json=body, timeout=self._timeout_s)
            response.raise_for_status()
        except requests.HTTPError as exc:
            # тело ответа — единственный способ увидеть причину 4xx из логов
            # (например, отключённый allow_structured_metadata → 400 на каждый пуш)
            log.exception(
                "Loki отверг строку (%s, labels=%s): HTTP %s, body=%r",
                self._url, effect.labels,
                exc.response.status_code if exc.response is not None else "?",
                exc.response.text[:500] if exc.response is not None else "",
            )
        except requests.RequestException:
            log.exception("не удалось отправить строку в Loki (%s, labels=%s)", self._url, effect.labels)
