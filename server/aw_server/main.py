"""Точка входа aw-server: мост Лог-потока (issue #14) + телеграм-бот (issue #15) —
одним процессом на общем MQTT-соединении и общем ядре-роутере (core.Router)."""

from __future__ import annotations

import asyncio
import logging
import time
from datetime import datetime, timedelta, timezone

from . import config
from .core import Router
from .loki import HttpLokiPort
from .mqtt_bridge import MqttBridge, MqttCmdPort
from .telegram_bot import PtbTelegramPort, build_application, register_handlers

log = logging.getLogger("aw_server")

HEARTBEAT_PATH = "/tmp/healthy"
HEARTBEAT_INTERVAL_S = 10


def _touch_heartbeat() -> None:
    # читается HEALTHCHECK контейнера: показывает, что главный поток жив, а не что
    # брокер/Loki/Telegram доступны — их недоступность не должна валить контейнер (#14)
    with open(HEARTBEAT_PATH, "w") as f:
        f.write(str(time.time()))


async def _hourly_summary_loop(router: Router, telegram_port: PtbTelegramPort, interval_s: int) -> None:
    while True:
        await asyncio.sleep(interval_s)
        # create_task без наблюдателя: неожиданное исключение молча убило бы
        # сводки навсегда (heartbeat остался бы здоровым) — ловим и живём дальше
        try:
            text = router.hourly_summary_text(datetime.now(timezone.utc))
            if text is None:
                log.info("часовая сводка пропущена: стейта нет или он устарел")
                continue
            await telegram_port.broadcast_async(text)
        except asyncio.CancelledError:
            raise
        except Exception:
            log.exception("ошибка часовой сводки, следующая попытка через %dс", interval_s)


async def _run(cfg: config.Config, router: Router, loki_port: HttpLokiPort) -> None:
    application = build_application(cfg.telegram_bot_token)

    loop = asyncio.get_running_loop()
    telegram_port = PtbTelegramPort(application.bot, cfg.telegram_whitelist_chat_ids, loop)

    bridge = MqttBridge(cfg, router, loki_port, telegram_port)
    cmd_port = MqttCmdPort(bridge.client, bridge.cmd_topic)
    register_handlers(application, router, cmd_port)

    bridge.start()

    # Официальный паттерн PTB для встраивания Application в свой asyncio-луп рядом
    # с другим кодом (paho работает в собственном потоке, часовая сводка — своя
    # задача в этом же лупе) — не используем блокирующий Application.run_polling().
    async with application:
        await application.start()
        await application.updater.start_polling()
        log.info(
            "aw-server запущен: log-bridge (%slog/#) + телеграм-бот (whitelist: %d чат(ов))",
            cfg.mqtt_topic_prefix, len(cfg.telegram_whitelist_chat_ids),
        )
        # HOURLY_SUMMARY_INTERVAL_S <= 0 — периодическая сводка отключена
        # (введено по просьбе владельца; вернуть — выставить интервал в env)
        summary_task = None
        if cfg.hourly_summary_interval_s > 0:
            summary_task = asyncio.create_task(
                _hourly_summary_loop(router, telegram_port, cfg.hourly_summary_interval_s)
            )
        else:
            log.info("периодическая сводка отключена (HOURLY_SUMMARY_INTERVAL_S=%d)",
                     cfg.hourly_summary_interval_s)
        try:
            while True:
                _touch_heartbeat()
                await asyncio.sleep(HEARTBEAT_INTERVAL_S)
        finally:
            if summary_task is not None:
                summary_task.cancel()
            await application.updater.stop()
            await application.stop()


def main() -> None:
    cfg = config.load()
    logging.basicConfig(level=cfg.log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    # httpx (используется python-telegram-bot) на INFO логирует полный URL запроса,
    # включая TELEGRAM_BOT_TOKEN прямо в пути (https://api.telegram.org/bot<token>/...) —
    # такое нельзя пускать в docker logs/journald. Токен не секрет уровня пароля к БД,
    # но даёт полный контроль над ботом, поэтому глушим отдельно от общего log_level.
    # httpcore и telegram могут светить те же URL на DEBUG — глушим и их (ревью GLM).
    for noisy in ("httpx", "httpcore", "telegram"):
        logging.getLogger(noisy).setLevel(logging.WARNING)

    loki_port = HttpLokiPort(cfg.loki_url)
    router = Router(
        log_topic_prefix=cfg.mqtt_topic_prefix + "log/",
        whitelist_chat_ids=cfg.telegram_whitelist_chat_ids,
        state_freshness=timedelta(hours=cfg.state_freshness_hours),
        plant_names=cfg.plant_names,
    )

    asyncio.run(_run(cfg, router, loki_port))


if __name__ == "__main__":
    main()
