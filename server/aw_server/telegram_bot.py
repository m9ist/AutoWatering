"""Транспортный адаптер: python-telegram-bot <-> Router <-> MqttCmdPort/TelegramBroadcastPort.

Тонкая обвязка команд: парсинг, whitelist и валидация — в core.Router (см.
tests/test_core.py). Обработчики здесь только достают chat_id/аргументы из Update
и применяют CommandResult к портам (ответ пользователю, публикация в aw/cmd).

Живой Telegram API здесь не тестируется юнит-тестами — проверяется по критериям
тикета на деплое (getMe, реальные сообщения в whitelist-чат).
"""

from __future__ import annotations

import asyncio
import logging
from datetime import datetime, timezone

from telegram import Bot, Update
from telegram.ext import Application, CommandHandler, ContextTypes

from .core import CommandResult, Router
from .mqtt_bridge import MqttCmdPort

log = logging.getLogger(__name__)


def build_application(token: str) -> Application:
    return Application.builder().token(token).build()


def register_handlers(application: Application, router: Router, cmd_port: MqttCmdPort) -> None:
    async def water(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        await _apply(update, router.handle_water(_chat_id(update), context.args, _now()), cmd_port)

    async def config_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        await _apply(update, router.handle_config(_chat_id(update), context.args, _now()), cmd_port)

    async def daily(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        await _apply(update, router.handle_daily(_chat_id(update), _now()), cmd_port)

    async def checkvalves(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        await _apply(update, router.handle_checkvalves(_chat_id(update), _now()), cmd_port)

    async def graphs(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        await _apply(update, router.handle_graphs(_chat_id(update), _now()), cmd_port)

    async def state(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        await _apply(update, router.handle_state(_chat_id(update), _now()), cmd_port)

    async def help_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        await _apply(update, router.handle_help(_chat_id(update)), cmd_port)

    application.add_handler(CommandHandler("water", water))
    application.add_handler(CommandHandler("config", config_cmd))
    application.add_handler(CommandHandler("daily", daily))
    application.add_handler(CommandHandler("checkvalves", checkvalves))
    application.add_handler(CommandHandler("graphs", graphs))
    application.add_handler(CommandHandler("state", state))
    application.add_handler(CommandHandler("help", help_cmd))


def _chat_id(update: Update) -> str:
    return str(update.effective_chat.id)


def _now() -> datetime:
    return datetime.now(timezone.utc)


async def _apply(update: Update, result: CommandResult, cmd_port: MqttCmdPort) -> None:
    if result.ignored:
        # Защита: команды принимаются только из чатов whitelist (issue #15,
        # критерий "чужие чаты игнорируются") — не отвечаем, только логируем.
        log.info("игнорируем команду из чужого чата %s: %r", _chat_id(update), update.message.text)
        return
    if result.cmd_payload is not None:
        if not cmd_port.publish(result.cmd_payload):
            # не подтверждаем то, что не ушло (ревью #15: «ложный ACK»)
            await update.message.reply_text(
                "Команда НЕ отправлена: брокер недоступен, попробуй позже."
            )
            return
    if result.reply_text is not None:
        await update.message.reply_text(result.reply_text)


class PtbTelegramPort:
    """Порт вывода: рассылка текста во все whitelist-чаты (aw/event, часовая сводка).

    broadcast() вызывается как из потока paho (не-asyncio, см. mqtt_bridge.MqttBridge),
    так и из asyncio-кода этого же процесса (часовая сводка) — run_coroutine_threadsafe
    безопасен для вызова с любого потока.
    """

    def __init__(self, bot: Bot, whitelist_chat_ids: frozenset[str], loop: asyncio.AbstractEventLoop) -> None:
        self._bot = bot
        self._whitelist = whitelist_chat_ids
        self._loop = loop

    def broadcast(self, text: str) -> None:
        asyncio.run_coroutine_threadsafe(self.broadcast_async(text), self._loop)

    async def broadcast_async(self, text: str) -> None:
        for chat_id in self._whitelist:
            try:
                await self._bot.send_message(chat_id=chat_id, text=text)
            except Exception:
                log.exception("не удалось отправить сообщение в чат %s", chat_id)
