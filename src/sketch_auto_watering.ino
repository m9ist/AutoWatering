#include <ArduinoJson.h>
#include <AwClock.h>
#include <AwLogging.h>
#include <Communication.h>
#include <Ds1302.h>
#include <EEPROM.h>
#include <Pomp.h>
#include <Screen.h>
#include <Sensors.h>
#include <State.h>
#include <Time.h>
#include <Timer.h>
#include <avr/wdt.h>

#define IS_DEBUG true

// Alt + Shift + F - автоформатирование кода
// Ctrl + Alt + B - компиляция
// Ctrl + Alt + U - загрузка прошивки

State global_state;
AwLogging logger;
Sensors sensors;
AwClock awClock;
Pomp pomp;

void stateUpdated() {
  // todo подумать о том, чтобы сохранять источник, время, "широту обновления"
  global_state.updated = true;
}

Timer timerSensorsCheck;
const Duration repeatIntervalSensorsCheck = Timer::Minutes(1);
Communication comm = Communication(Serial3, Serial, false);

void loadStateEEPROM() {
  int address = 0;
  int version;
  EEPROM.get(address, version);
  address += sizeof(int);
  int storedSize;
  EEPROM.get(address, storedSize);
  address += sizeof(int);
  // размер State проверяем вместе с версией: страховка от забытого
  // bump'а EEPROM_VERSION при изменении структуры
  if (version != EEPROM_VERSION || storedSize != (int)sizeof(State)) {
    Serial.println("Bad version of EEPROM in memory");
  } else {
    EEPROM.get(address, global_state);
    // activePump идёт индексом в lastPumpMl/lastPumpRunAt: перевёрнутый бит в
    // этом байте (версия и размер при этом целы) дал бы запись мимо массива,
    // в соседние поля State, и она осталась бы в EEPROM (ревью GLM)
    if (global_state.activePump > PUMP_2) global_state.activePump = PUMP_2;
    Serial.println("Loaded state from eeprom");
  }
}

void saveStateEEPROM() {
  int address = 0;
  int version = EEPROM_VERSION;
  EEPROM.put(address, version);
  address += sizeof(int);
  EEPROM.put(address, (int)sizeof(State));
  address += sizeof(int);
  EEPROM.put(address, global_state);
#ifdef DEBUG_LOG
  logger.writeln(F("Saved state to eeprom"));
#endif
}

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

void logFreeRam() {
  global_state.freeMemorySize = freeRam();
  logger.logFreeRam(global_state.freeMemorySize);
}

// Долгий delay с периодическими wdt_reset. WDT настроен на 8 секунд
// (см. setup), поэтому простой delay(N) при N >= 8000 кикает плату.
// Шаг 1с — есть запас, и резет идёт перед сном, между кусками и в конце.
void sleepWithWdt(unsigned long ms) {
  const unsigned long CHUNK_MS = 1000;
  wdt_reset();
  unsigned long elapsed = 0;
  while (elapsed < ms) {
    unsigned long step = (ms - elapsed) < CHUNK_MS ? (ms - elapsed) : CHUNK_MS;
    delay(step);
    wdt_reset();
    elapsed += step;
  }
}

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);
  while (!Serial || !Serial3) {
  }

  Serial.println(F("Start working"));
  wdt_enable(WDTO_8S);
  logFreeRam();
  loadStateEEPROM();
  global_state.sdInited = false;
  global_state.espConnectedAndTimeSynced = false;
  logger.init(global_state);
  awClock.initClock(global_state, logger);
  initScreen(logger);

  sensors.init(logger, global_state);
  pomp.initPomp(logger);
  pomp.updatePlantsState(global_state);

  timerSensorsCheck.setDuration(Timer::Seconds(5));
  logger.writeln(F("End init arduino"));
}

uint32_t test_time = 0;

void sendTelegram(String message) {
  JsonDocument toSend;
  toSend[COMMAND_KEY] = ARDUINO_SEND_TELEGRAM;
  toSend[F("message")] = (String)F("Arduino: ") + message;
  String out;
  serializeJson(toSend, out);
#ifdef DEBUG_LOG
  logger.writeln(out);
#endif
  comm.communicationSendMessage(out);
  logFreeRam();
}

void waterPlant(int id, int amount) {
  String info =
      pomp.waterPlant(id, amount, global_state, dateToEpoch(awClock.getNow()),
                      logger);
  // результат пуска помпы лёг в стейт (issue #22) — сохраняем и отдаём наверх
  saveStateEEPROM();
  stateUpdated();
  logFreeRam();
  sendTelegram(info);
  delay(10);
}

// Дневной полив держит loop() минутами, и без этого отчёты копились в очереди
// и уходили в ESP пачкой в конце — на такой пачке связь срывалась и отчёты
// терялись. Один тик отправляет не больше одного сообщения, а может уйти на
// чтение входящего или на восстановление связи, поэтому тиков несколько.
void flushCommunication() {
  for (int i = 0; i < 3; i++) {
    comm.communicationTick();
    wdt_reset();
  }
}

void runDailyCommand() {
  logger.buzzerCommand();
  sendTelegram(F("Start daily task."));
  flushCommunication();
  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    logFreeRam();
    const Plant& plant = global_state.plants[i];
    if (plant.isOn != PLANT_IS_ON || plant.dailyAmountMl <= 0) {
      continue;
    }
    waterPlant(i, plant.dailyAmountMl);
    flushCommunication();
  }
  sendTelegram((String)F("Daily task is completed. Free mem = ") + freeRam());
}

// Валидация команд с plantId/amountMl: id за пределами массива plants — это
// запись в чужую память и bitWrite с UB (мог открыться случайный клапан),
// amount без лимита — потоп от опечатки в Telegram.
// long, а не int: на AVR int 16-битный, plant65536 усёкся бы до валидного
// id=0 ещё до проверки.
bool isValidPlantCommand(long id, long amount) {
  if (id < 0 || id >= PLANTS_AMOUNT) {
    sendTelegram((String)F("Rejected: bad plant id ") + id);
    return false;
  }
  if (amount < 0 || amount > MAX_WATER_AMOUNT_ML) {
    sendTelegram((String)F("Rejected: bad amount ") + amount + F("ml, max ") +
                 MAX_WATER_AMOUNT_ML + F("ml"));
    return false;
  }
  return true;
}

// Границы параметров оживления. Прогон по времени не ограничен (кнопка крутит
// формулу, пока её держат) — это отсев опечаток на входе, как у amountMl:
// "3x20000" означал бы клапан под напряжением 20 секунд подряд.
bool isValidWakeupCommand(long id, long longCycles, long longOnMs,
                          long shortCycles, long shortOnMs) {
  if (id < 0 || id >= PLANTS_AMOUNT) {
    sendTelegram((String)F("Rejected: bad plant id ") + id);
    return false;
  }
  if (longCycles < 0 || longCycles > MAX_WAKEUP_CYCLES || shortCycles < 0 ||
      shortCycles > MAX_WAKEUP_CYCLES || longOnMs < 0 ||
      longOnMs > MAX_WAKEUP_ON_MS || shortOnMs < 0 ||
      shortOnMs > MAX_WAKEUP_ON_MS) {
    sendTelegram((String)F("Rejected: bad wakeup params, max ") +
                 MAX_WAKEUP_CYCLES + F(" cycles, ") + MAX_WAKEUP_ON_MS +
                 F("ms on"));
    return false;
  }
  return true;
}

// Оживление клапана с кнопки: тумблер мотора выключен, значит человек просит
// не полив, а расхаживание клапана. Формула крутится по кругу, пока кнопку
// держат; один полный проход отрабатывает в любом случае, даже если кнопку
// отпустили сразу. Помпа не запускается — режим всегда сухой.
// Стейт и EEPROM не трогаем: оживление не оставляет следа, только отчёт.
void wakeupPlantByButton(int id) {
  drawScreenMessage((String)F("Wakeup plant ") + id, logger);
  Pomp::WakeupRun run = pomp.beginWakeup(logger);
  do {
    pomp.runWakeupFormula(id, WAKEUP_LONG_CYCLES, WAKEUP_LONG_ON_MS,
                          WAKEUP_SHORT_CYCLES, WAKEUP_SHORT_ON_MS, run, logger);
  } while (pomp.isWaterNowButtonPressed(id));
  String info = pomp.buildWakeupReport(id, run);
  // Отчёт остаётся на экране: loopScreen() здесь затёр бы его за десятки
  // миллисекунд, а паузу на чтение сознательно не ставим — после прогона
  // можно сразу браться за соседний горшок. Экран вернётся к обычному виду
  // на ближайшей проверке датчиков.
  drawScreenMessage(info, logger);
  sendTelegram(info);
  logFreeRam();
}

void processEspCommand(JsonDocument& doc) {
  const char* command = doc[COMMAND_KEY];
  if ((String)ESP_COMMAND_LOG == command) {
    // уже залогировали, ничего не делаем
    return;
  }

  if ((String)ESP_COMMAND_TIME_SYNCED == command) {
    tm timeinfo = deserializeTimeInfo(doc);
    awClock.setupDate(timeinfo, logger);
    global_state.espConnectedAndTimeSynced = true;
    loopScreen(global_state);
    return;
  }

  if ((String)ESP_COMMAND_WATER_PLANT == command) {
    long id = doc[F("plantId")];
    long amount = doc[F("amountMl")];
    if (!isValidPlantCommand(id, amount)) return;
    logger.buzzerCommand();
    waterPlant(id, amount);
    return;
  }

  if ((String)ESP_COMMAND_CONFIG_PLANT == command) {
    long id = doc[F("plantId")];
    long amount = doc[F("amountMl")];
    if (!isValidPlantCommand(id, amount)) return;
    global_state.plants[id].dailyAmountMl = amount;
    saveStateEEPROM();
    // snprintf в буфер вместо конкатенации String: меньше реаллокаций кучи,
    // прошлая версия на String с F() зависала (фрагментация)
    char info[340];
    int pos = snprintf_P(info, sizeof(info), PSTR("Current config: "));
    for (int i = 0; i < PLANTS_AMOUNT; i++) {
      if (global_state.plants[i].dailyAmountMl > 0 &&
          pos < (int)sizeof(info)) {
        pos += snprintf_P(info + pos, sizeof(info) - pos,
                          PSTR("plant %d = %d ml, "), i,
                          global_state.plants[i].dailyAmountMl);
      }
    }
    sendTelegram(info);
    return;
  }

  if ((String)ESP_COMMAND_DAILY_TASK == command) {
    runDailyCommand();
    return;
  }

  if ((String)ESP_COMMAND_CHECK_VALVES == command) {
    sendTelegram(pomp.checkAllActiveValves(global_state, logger));
    return;
  }

  if ((String)ESP_COMMAND_WAKEUP == command) {
    long id = doc[F("plantId")];
    long longCycles = doc[F("longCycles")];
    long longOnMs = doc[F("longOnMs")];
    long shortCycles = doc[F("shortCycles")];
    long shortOnMs = doc[F("shortOnMs")];
    if (!isValidWakeupCommand(id, longCycles, longOnMs, shortCycles,
                              shortOnMs)) {
      return;
    }
    // Статус растения не смотрим (в отличие от checkAllActiveValves): команда
    // адресная, а дольше всех простаивает как раз клапан выключенного горшка.
    logger.buzzerCommand();
    drawScreenMessage((String)F("Wakeup plant ") + id, logger);
    Pomp::WakeupRun run = pomp.beginWakeup(logger);
    pomp.runWakeupFormula(id, longCycles, longOnMs, shortCycles, shortOnMs, run,
                          logger);
    String info = pomp.buildWakeupReport(id, run);
    // Экран не перерисовываем — см. wakeupPlantByButton
    drawScreenMessage(info, logger);
    sendTelegram(info);
    return;
  }

  if ((String)ESP_COMMAND_SWITCH_PUMP == command) {
    // Переключение на другую помпу (issue #22). Эха в Telegram нет —
    // поднимаем updated, результат человек смотрит через /state.
    global_state.activePump =
        global_state.activePump == PUMP_1 ? PUMP_2 : PUMP_1;
    saveStateEEPROM();
    stateUpdated();
    logger.writeln((String)F("Active pump switched to ") +
                   (global_state.activePump + 1));
    return;
  }

#ifdef DEBUG_LOG
  logger.writeln((String)F("Unknown command ") + command);
#endif
}

void loop() {
  wdt_reset();
  // сначала делаем дешевые операции, все дорогие делаем в конце функции
  comm.communicationTick();
  wdt_reset();

  if (comm.communicationHasMessage()) {
    String message = comm.communicationGetMessage();
#ifdef DEBUG_LOG
    logger.writeln(message);
#endif
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error != DeserializationError::Ok) {
      logger.writeln((String)F("Can't deserialize ") + error.c_str());
    } else {
      processEspCommand(doc);
    }
    logFreeRam();
    return;
  }

  if (global_state.updated) {
    logFreeRam();
    // часы Mega в кадр: по ним aw-server считает возраст пусков помп, не
    // сводя своё время с RTC ардуины (issue #22)
    global_state.nowEpoch = dateToEpoch(awClock.getNow());
    JsonDocument toSend = serializeState(global_state);
    String out;
#ifdef DEBUG_LOG
    logger.writeln((String)F("Expected string length ") +
                   (measureJson(toSend) + 1));
#endif
    serializeJson(toSend, out);
#ifdef DEBUG_LOG
    logger.writeln(out);
#endif
    comm.communicationSendMessage(out);
    global_state.updated = false;
    // todo придумать более красивую схему обновления экрана
    loopScreen(global_state);
    logFreeRam();
    return;
  }

  for (int i = 0; i < 16; i++) {
    if (pomp.isWaterNowButtonPressed(i)) {
      // Тумблер мотора читаем один раз, в момент нажатия: выключен — кнопка
      // оживляет клапан, включен — поливает как раньше. Щелчок тумблером
      // посреди прогона режим не меняет.
      if (!pomp.isPompSwitchOn()) {
        wakeupPlantByButton(i);
        return;
      }
      drawScreenMessage((String)F("Start water plant ") + i, logger);
      // Кнопка на корпусе — всегда активная помпа, без пробы резерва: ты
      // стоишь рядом с горшком и ждёшь воды, получить в этот момент пуск
      // дохлого резерва — худший вариант (issue #22).
      uint8_t pumpIdx = global_state.activePump;
      pomp.beginWateringAmpStats(logger);
      pomp.beforeLoopFlowSensor();
      pomp.startWaterPlant(i, pumpIdx, logger);

      while (pomp.isWaterNowButtonPressed(i)) {
        wdt_reset();
        pomp.loopFlowSensor();
        pomp.sampleWateringAmpIfNeeded(logger);
      }

      unsigned long actualMs = pomp.stopWaterPlant(i, logger);
      float realMl = pomp.getWaterFlowSensorMl();
      String info = pomp.buildWaterReport(i, pumpIdx, /*spareProbe=*/false,
                                          /*requestedMl=*/-1, actualMs, realMl,
                                          pomp.getWateringAmpDelta());
      pomp.recordPumpRun(global_state, pumpIdx, realMl,
                         dateToEpoch(awClock.getNow()));
      saveStateEEPROM();
      stateUpdated();
      drawScreenMessage(info, logger);
      sendTelegram(info);
      // todo <<<<<< подумать как отказаться от этого, обдумать всю схему работы
      // с экраном
      sleepWithWdt(3000);

      loopScreen(global_state);
      return;
    }
  }

  bool wasUpdate = pomp.updatePlantsState(global_state);
  if (wasUpdate) {
    stateUpdated();
    saveStateEEPROM();
  }

  if (timerSensorsCheck.expired()) {
    timerSensorsCheck.setDuration(repeatIntervalSensorsCheck);
    sensors.loopSensors(logger, global_state);
    global_state.lastSensorCheck = awClock.getNow();
    stateUpdated();
    return;
  }

  if (isCheckButtonPressed()) {
    String info = pomp.checkAllActiveValves(global_state, logger);
    drawScreenMessage(info, logger);
    sendTelegram(info);
    sleepWithWdt(5000);
    loopScreen(global_state);
    return;
  }
}