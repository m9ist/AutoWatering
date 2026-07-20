#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

// Разбор Команды из aw/cmd (issue #17): чистая логика без Arduino-зависимостей
// (по образцу LogRing.h) — тестируется native (test/test_cmd_handler).
// ArduinoJson — не Arduino-специфичная библиотека (чистый C++), собирается и
// на десктопе без Arduino.h.
//
// Разбито на два независимых шага, как и было при telegram-канале (issue
// #13): decideCmd() распознаёт команду и достаёт поля (без проверки границ),
// isPlantCommandInBounds() — отдельно чистая версия проверки границ id/объёма.
// EspMain.cpp вызывает их через checkPlantCommandBounds (сохранённая функция,
// её сайд-эффект — serialLog на отказ — не меняем) и добавляет aw/event.
//
// Имена команд дублируют src/State.h (COMMAND_KEY="c", ESP_COMMAND_*) — не
// подключаем реальный State.h напрямую, потому что он тянет Ds1302.h/Time.h
// (Arduino-only). Если имена команд в State.h поменяются, обновить и тут (то
// же соглашение, что в server/aw_server/core.py про #17 — контракт передаётся
// байт в байт, не переформатируется).
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace cmdhandler {

constexpr const char* kCmdWater = "esp_water";
constexpr const char* kCmdConfig = "esp_plant_conf";
constexpr const char* kCmdDaily = "esp_daily";
constexpr const char* kCmdCheckValves = "esp_check_valves";
// графики не форвардятся на Mega — рендерятся на ESP из Graph/PointsHoler и
// отвечают через aw/event (имя команды ввёл бот, см. server/aw_server/core.py)
constexpr const char* kCmdGraphs = "esp_graphs";

enum class Action {
  kWater,        // форвард в UART: serialPlantCommand(ESP_COMMAND_WATER_PLANT, ...)
  kConfig,       // форвард в UART: serialPlantCommand(ESP_COMMAND_CONFIG_PLANT, ...)
  kDaily,        // форвард в UART без id/amount
  kCheckValves,  // форвард в UART без id/amount
  kGraphs,       // рендер графиков локально на ESP, ответ в aw/event
  kReject,       // отказ на уровне JSON/имени команды, текст — в reason
};

struct Decision {
  Action action = Action::kReject;
  // для kWater/kConfig — как есть в Команде, границы НЕ проверены (это
  // отдельно, см. isPlantCommandInBounds ниже); -1, если поле отсутствует
  long plantId = -1;
  long amountMl = -1;
  // человекочитаемый текст отказа (для aw/event type=reject и лога),
  // заполнен только при action == kReject
  char reason[96] = "";
};

// Распознаёт команду и достаёт plantId/amountMl. Границы значений здесь не
// проверяются — вызывающая сторона обязана прогнать kWater/kConfig через
// isPlantCommandInBounds (или checkPlantCommandBounds на ESP) перед форвардом.
inline Decision decideCmd(const char* payload, size_t length) {
  Decision d;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    snprintf(d.reason, sizeof(d.reason), "Bad JSON in aw/cmd: %s",
             err.c_str());
    return d;
  }

  const char* command = doc["c"];
  if (command == nullptr) {
    snprintf(d.reason, sizeof(d.reason), "aw/cmd: missing 'c'");
    return d;
  }

  bool isWater = strcmp(command, kCmdWater) == 0;
  bool isConfig = strcmp(command, kCmdConfig) == 0;
  if (isWater || isConfig) {
    d.action = isWater ? Action::kWater : Action::kConfig;
    d.plantId = doc["plantId"] | -1L;
    d.amountMl = doc["amountMl"] | -1L;
    return d;
  }

  if (strcmp(command, kCmdDaily) == 0) {
    d.action = Action::kDaily;
    return d;
  }
  if (strcmp(command, kCmdCheckValves) == 0) {
    d.action = Action::kCheckValves;
    return d;
  }
  if (strcmp(command, kCmdGraphs) == 0) {
    d.action = Action::kGraphs;
    return d;
  }

  snprintf(d.reason, sizeof(d.reason), "Unknown aw/cmd command: %s", command);
  return d;
}

// Чистая версия проверки границ id/объёма — та же логика, что раньше жила
// прямо в checkPlantCommandBounds (EspMain.cpp, issue #13). plantsAmount/
// maxWaterAmountMl передаются вызывающей стороной (см. PLANTS_AMOUNT/
// MAX_WATER_AMOUNT_ML из src/State.h) — этот модуль их не хардкодит заново.
inline bool isPlantCommandInBounds(long id, long amountMl, int plantsAmount,
                                   int maxWaterAmountMl) {
  return id >= 0 && id < plantsAmount && amountMl >= 0 &&
         amountMl <= maxWaterAmountMl;
}

}  // namespace cmdhandler

#endif  // CMD_HANDLER_H
