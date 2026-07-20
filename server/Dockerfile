# aw-server — ядро-роутер + мост логов ESP в Loki (issue #14).
# Пин по digest от 2026-07-20 (тег 3.12-slim).
FROM python:3.12-slim@sha256:57cd7c3a7a273101a6485ba99423ee568157882804b1124b4dd04266317710de

WORKDIR /app

COPY pyproject.toml ./
COPY aw_server ./aw_server

RUN pip install --no-cache-dir .

USER 10008:10008

# HEALTHCHECK проверяет, что главный цикл жив (см. main._touch_heartbeat), а не что
# брокер/Loki доступны — недоступность брокера/Loki не должна валить контейнер (#14)
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD python -c "import sys,time; sys.exit(0 if time.time()-float(open('/tmp/healthy').read())<30 else 1)"

CMD ["python", "-m", "aw_server.main"]
