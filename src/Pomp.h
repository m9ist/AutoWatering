#ifndef POMP_H
#define POMP_H
#include <AwLogging.h>
#include <CurrentSensor.h>
#include <FlowMeter.h>
#include <State.h>
#include <Valves.h>
#include <avr/wdt.h>

// общий на оба мультиплексера, тк читать будет по очереди
#define PIN_PLANT_MULTIPLEXER_S0 A11
#define PIN_PLANT_MULTIPLEXER_S1 A10
#define PIN_PLANT_MULTIPLEXER_S2 A9
#define PIN_PLANT_MULTIPLEXER_S3 A8

#define PIN_MULTIPLEXER_WATER_NOW_SIG 20
#define PIN_MULTIPLEXER_PLANT_TURN_ON_SIG 18

#define PIN_POMP_MAIN 6
#define PIN_POMP_SPARE 5
#define PIN_POMP_TURN_ON 17

#define POMP_SPEED_LOW 80
#define POMP_SPEED_MEDIUM 140
#define POMP_SPEED_HIGH 180

// Проба резерва (issue #22): доля поливов, которые уходят на неактивную
// помпу — чтобы знать, жива ли она, до того как умрёт активная.
#define SPARE_PROBE_PERCENT 10

#define WATER_FLOW_ITERATION_MS 100

// Оркестратор полива: насос, кнопки/тумблеры через мультиплексеры и
// сценарии полива. Клапаны, расходомер и датчик тока — отдельные
// модули (Valves, FlowMeter, CurrentSensor), Pomp их координирует.
class Pomp {
 public:
  // Накопитель одного прогона оживления: кнопка крутит формулу по кругу,
  // пока её держат, поэтому счётчик циклов и дельты живут снаружи проходов.
  struct WakeupRun {
    // unsigned: кнопка не ограничена по времени, залипшая дала бы на int
    // знаковое переполнение (UB) примерно через 10 часов удержания
    unsigned int cycles = 0;
    int baseline = 0;
    int firstDelta = 0;
    int lastDelta = 0;
  };

 private:
  bool pompState = false;
  int plantsToButton[PLANTS_AMOUNT] = {1, 3, 5, 7, 8, 10, 12, 14,
                                       0, 2, 4, 6, 9, 11, 13, 15};
  unsigned long timeCheck;

  int currentPomp;
  bool acsPrimed = false;
  bool randomSeeded = false;

  Valves valves;
  FlowMeter flowMeter;
  CurrentSensor currentSensor;

  // Сид для пробы резерва берём при первом поливе, а не в setup(): весь код
  // до setup() выполняется одинаково от ребута к ребуту, и сид там был бы
  // детерминированным. Момент первого полива — настоящая асинхронная
  // энтропия: планировщика в прошивке нет, полив всегда инициирует человек
  // или сервер. analogRead ACS712 подмешивает шум реального аналогового
  // входа (свободные A3-A7 на разведённой плате болтались бы в воздухе и
  // дали бы меньше).
  void seedRandomIfNeeded(AwLogging& logger) {
    if (randomSeeded) return;
    unsigned long seed = micros() ^ (unsigned long)analogRead(PIN_AMPERAGE_SENSOR);
    randomSeed(seed);
    randomSeeded = true;
    logger.writeln((String)F("Random seeded: ") + seed);
  }

  void startPomp(uint8_t pumpIdx, AwLogging& logger) {
    logger.writeln((String)F("Start pomp ") + (pumpIdx + 1));
    currentPomp = pumpPin(pumpIdx);
    analogWrite(currentPomp, POMP_SPEED_LOW);
    delay(30);
    analogWrite(currentPomp, POMP_SPEED_MEDIUM);
    delay(20);
    analogWrite(currentPomp, POMP_SPEED_HIGH);
  }

  void stopPomp(AwLogging& logger) {
    logger.writeln(F("Stop pomp"));
    analogWrite(currentPomp, POMP_SPEED_LOW);
    delay(50);
    digitalWrite(currentPomp, LOW);
  }

  // Позиционный номер помпы -> пин. Наружу (Telegram, aw/state) помпы
  // известны как pump1/pump2 и нумеруются позиционно; пины не покидают
  // этот модуль.
  int pumpPin(uint8_t pumpIdx) {
    return pumpIdx == PUMP_1 ? PIN_POMP_MAIN : PIN_POMP_SPARE;
  }

  void multiplexPlant(int id) {
    digitalWrite(PIN_PLANT_MULTIPLEXER_S0, bitRead(id, 0));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S1, bitRead(id, 1));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S2, bitRead(id, 2));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S3, bitRead(id, 3));
  }

  // Холостой ON-OFF одного клапана — поднимает «фоновый ток» в линии
  // до стабильного уровня. Делается один раз за boot.
  // Подробности эффекта — см. tasks/2026-05-24_valve_detect_pattern.md.
  void primeAcsLineIfNeeded(AwLogging& logger) {
    if (acsPrimed) return;
    const int PRIMER_SLOT = 0;
    const int PRIMER_ON_MS = 150;
    const int PRIMER_SETTLE_MS = 200;
    logger.writeln(F("ACS primer: warming up baseline"));
    valves.turnOn(PRIMER_SLOT, logger);
    delay(PRIMER_ON_MS);
    valves.turnOff(PRIMER_SLOT, logger);
    delay(PRIMER_SETTLE_MS);
    acsPrimed = true;
  }

  // Один цикл оживления: открыть клапан на onMs, закрыть на WAKEUP_OFF_MS.
  // Ток снимается в том же окне, что и в checkValveConnected: сначала
  // ON_DELAY_MS, потом 5 замеров. Соленоиду нужно ~30мс на выход на ток
  // (tasks/2026-05-24: on[0] ниже on[1] на 100-250mA), а порог всего 50mA —
  // замер с t=0 затянул бы среднее вниз и дал бы ложный DISCONNECTED.
  void wakeupCycle(int id, int onMs, WakeupRun& run, AwLogging& logger) {
    const int ON_DELAY_MS = 50;
    const int ACTIVE_SAMPLES = 5;
    const int MEASURE_MS = ON_DELAY_MS + ACTIVE_SAMPLES * 10;
    valves.turnOn(id, logger);
    delay(ON_DELAY_MS);
    int delta = currentSensor.measureDcAvg(ACTIVE_SAMPLES) - run.baseline;
    if (onMs > MEASURE_MS) delay(onMs - MEASURE_MS);
    valves.turnOff(id, logger);
    wdt_reset();
    delay(WAKEUP_OFF_MS);
    wdt_reset();
    if (run.cycles == 0) run.firstDelta = delta;
    run.lastDelta = delta;
    run.cycles++;
  }

 public:
  Pomp(/* args */) {};
  ~Pomp() {};

  void initPomp(AwLogging& logger) {
    pinMode(PIN_POMP_MAIN, OUTPUT);
    pinMode(PIN_POMP_SPARE, OUTPUT);

    pinMode(PIN_PLANT_MULTIPLEXER_S0, OUTPUT);
    pinMode(PIN_PLANT_MULTIPLEXER_S1, OUTPUT);
    pinMode(PIN_PLANT_MULTIPLEXER_S2, OUTPUT);
    pinMode(PIN_PLANT_MULTIPLEXER_S3, OUTPUT);

    pinMode(PIN_MULTIPLEXER_PLANT_TURN_ON_SIG, INPUT_PULLUP);
    pinMode(PIN_MULTIPLEXER_WATER_NOW_SIG, INPUT_PULLUP);

    pinMode(PIN_POMP_TURN_ON, INPUT_PULLUP);

    valves.init();
    flowMeter.init(logger);
    currentSensor.init(logger);
  }

  // обновляет включено ли юзером растение на тумблере
  // true - если было какое-то изменение
  bool updatePlantsState(State& state) {
    bool wasUpdate = false;
    bool v = digitalRead(PIN_POMP_TURN_ON) != HIGH;
    if (v) {
      if (!state.pompIsOn) {
        state.pompIsOn = true;
        wasUpdate = true;
      }
    } else {
      if (state.pompIsOn) {
        state.pompIsOn = false;
        wasUpdate = true;
      }
    }
    for (int i = 0; i < PLANTS_AMOUNT; i++) {
      multiplexPlant(i);
      v = digitalRead(PIN_MULTIPLEXER_PLANT_TURN_ON_SIG) != HIGH;
      if (v) {
        if (state.plants[i].isOn == PLANT_IS_OFF_USER ||
            state.plants[i].isOn == PLANT_IS_UNDEFINED) {
          state.plants[i].isOn = PLANT_IS_ON;
          wasUpdate = true;
        }
      } else {
        if (state.plants[i].isOn != PLANT_IS_OFF_USER) {
          state.plants[i].isOn = PLANT_IS_OFF_USER;
          wasUpdate = true;
        }
      }
    }
    return wasUpdate;
  }

  bool isWaterNowButtonPressed(int id) {
    int pinI = plantsToButton[id];
    multiplexPlant(pinI);
    int v = digitalRead(PIN_MULTIPLEXER_WATER_NOW_SIG);
    return v == LOW;
  }

  void startWaterPlant(int id, uint8_t pumpIdx, AwLogging& logger) {
    logger.writeln((String)F("Watering plant ") + id);
    timeCheck = millis();
    valves.turnOn(id, logger);
    // сделано, чтобы не создавать напряжение на клапанах
    delay(200);
    startPomp(pumpIdx, logger);
  }

  // Какой помпой поливать. С вероятностью SPARE_PROBE_PERCENT — неактивной:
  // это и есть проба резерва. Бросок на каждое растение, а не один на весь
  // дневной полив: при мёртвом резерве потерять один горшок из шестнадцати
  // лучше, чем все шестнадцать (issue #22).
  uint8_t pickPumpForWatering(const State& state, AwLogging& logger) {
    seedRandomIfNeeded(logger);
    if (random(100) < SPARE_PROBE_PERCENT) {
      uint8_t spare = state.activePump == PUMP_1 ? PUMP_2 : PUMP_1;
      logger.writeln((String)F("Spare probe: pump ") + (spare + 1));
      return spare;
    }
    return state.activePump;
  }

  // Результат пуска помпы в Стейт — по этим числам человек решает, жива ли
  // резервная (автоматического вердикта нет: ACS712 сидит на 5В-линии
  // клапанов, а помпа на отдельном 12В, ток про мотор ничего не говорит —
  // см. ADR-0002). Время приходит снаружи: у Pomp нет доступа к RTC.
  void recordPumpRun(State& state, uint8_t pumpIdx, float realMl,
                     uint32_t nowEpoch) {
    state.lastPumpMl[pumpIdx] = (uint16_t)realMl;
    state.lastPumpRunAt[pumpIdx] = nowEpoch;
  }

  // Возвращает реальную длительность полива в мс. Сводка собирается
  // на стороне вызывающего кода через buildWaterReport().
  unsigned long stopWaterPlant(int id, AwLogging& logger) {
    stopPomp(logger);
    // todo придумать более корректную схему <<<<<<<
    // сделано, чтобы не создавать напряжение на клапанах
    delay(200);
    valves.turnOff(id, logger);
    return millis() - timeCheck;
  }

  void beforeLoopFlowSensor() { flowMeter.beforeLoop(); }

  void loopFlowSensor() { flowMeter.loop(); }

  float getWaterFlowSensorMl() { return flowMeter.getMl(); }

  // Сброс перед поливом. Подтягивает фоновый ток (primer) и снимает
  // baseline, относительно которого считается дельта в отчёте.
  void beginWateringAmpStats(AwLogging& logger) {
    primeAcsLineIfNeeded(logger);
    currentSensor.beginWateringStats(logger);
  }

  void sampleWateringAmpIfNeeded(AwLogging& logger) {
    currentSensor.sampleIfNeeded(logger);
  }

  int getWateringAmpDelta() { return currentSensor.getWateringDelta(); }

  // Собирает унифицированную строку отчёта о поливе.
  // requestedMl < 0 — manual полив без заданного объёма.
  // ampDelta — средний ток поверх baseline (см. getWateringAmpDelta).
  // spareProbe — полив ушёл на неактивную помпу (проба резерва): без этой
  // пометки «Pump 1, Real ml = 0» не отличить от «переключился командой и
  // теперь всё льётся в пустоту».
  // snprintf в статический буфер вместо конкатенации String — меньше
  // реаллокаций кучи (фрагментация на 8КБ RAM).
  String buildWaterReport(int id, uint8_t pumpIdx, bool spareProbe,
                          int requestedMl, unsigned long actualMs, float realMl,
                          int ampDelta) {
    const char* valveStatus =
        ampDelta > CurrentSensor::VALVE_DELTA_THRESHOLD_MA ? "OK"
                                                           : "DISCONNECTED";
    const char* probeMark = spareProbe ? " (spare probe)" : "";
    char buf[160];
    if (requestedMl >= 0) {
      snprintf_P(buf, sizeof(buf),
                 PSTR("Done water id %d with %dml. Pump %d%s. Amperage delta: "
                      "%dmA (%s). Duration %lums. Real ml = %d"),
                 id, requestedMl, pumpIdx + 1, probeMark, ampDelta, valveStatus,
                 actualMs, (int)realMl);
    } else {
      snprintf_P(buf, sizeof(buf),
                 PSTR("Done water id %d. Pump %d%s. Amperage delta: %dmA (%s). "
                      "Duration %lums. Real ml = %d"),
                 id, pumpIdx + 1, probeMark, ampDelta, valveStatus, actualMs,
                 (int)realMl);
    }
    return String(buf);
  }

  String waterPlant(int id, int amounMl, State& state, uint32_t nowEpoch,
                    AwLogging& logger) {
    wdt_reset();
    float practicalSpeedMlInMs = 0.0077;
    // сколько итераций по 0.1 сек нужно сделать
    int expectedNumIterations =
        (float)amounMl / practicalSpeedMlInMs / WATER_FLOW_ITERATION_MS;
    logger.writeln((String)F("Num iterations = ") + expectedNumIterations);

    uint8_t pumpIdx = pickPumpForWatering(state, logger);
    bool spareProbe = pumpIdx != state.activePump;

    beginWateringAmpStats(logger);
    beforeLoopFlowSensor();
    unsigned long start = millis();
    startWaterPlant(id, pumpIdx, logger);

    for (int iter = 0; iter < expectedNumIterations; iter++) {
      while (start + WATER_FLOW_ITERATION_MS > millis() && start <= millis()) {
      }
      loopFlowSensor();
      sampleWateringAmpIfNeeded(logger);
      wdt_reset();
      start = millis();
    }

    unsigned long actualMs = stopWaterPlant(id, logger);
    float realMl = getWaterFlowSensorMl();
    recordPumpRun(state, pumpIdx, realMl, nowEpoch);
    return buildWaterReport(id, pumpIdx, spareProbe, amounMl, actualMs, realMl,
                            getWateringAmpDelta());
  }

  // Положение тумблера мотора прямо сейчас. Читается в момент нажатия
  // кнопки проливки: тумблер OFF -> кнопка запускает оживление клапана, а не
  // полив. Через state.pompIsOn не ходим — он обновляется отдельной веткой
  // loop() и на момент нажатия может отставать на итерацию.
  bool isPompSwitchOn() { return digitalRead(PIN_POMP_TURN_ON) != HIGH; }

  // Прогрев линии и baseline при закрытом клапане — тот же порядок, что у
  // checkValveConnected: без primer первый замер ловит «фоновый +18А».
  WakeupRun beginWakeup(AwLogging& logger) {
    primeAcsLineIfNeeded(logger);
    WakeupRun run;
    run.baseline = currentSensor.measureDcAvg(10);
    logger.writeln((String)F("Wakeup baseline: ") + run.baseline + F("mA"));
    return run;
  }

  // Один проход формулы оживления: longCycles «дожимов» по longOnMs (клапан
  // после простоя не открывается сразу, но открывается, если подержать
  // напряжение — этим лечим прикипание), затем shortCycles щелчков по
  // shortOnMs (расхаживаем ход). Помпа не запускается: режим всегда сухой.
  void runWakeupFormula(int id, int longCycles, int longOnMs, int shortCycles,
                        int shortOnMs, WakeupRun& run, AwLogging& logger) {
    for (int i = 0; i < longCycles; i++) {
      wakeupCycle(id, longOnMs, run, logger);
    }
    for (int i = 0; i < shortCycles; i++) {
      wakeupCycle(id, shortOnMs, run, logger);
    }
  }

  // Отчёт о прогоне. Про механику клапана ток не говорит ничего: соленоид
  // тянет постоянный ток всё время, пока на него подано напряжение, хоть
  // с прикипевшим штоком (tasks/2026-05-24: профиль плоский, inrush'а нет).
  // Поэтому вердикт — про электрику, «ожил или нет» человек решает по звуку
  // и по тому, пошла ли вода.
  String buildWakeupReport(int id, const WakeupRun& run) {
    const char* status =
        run.lastDelta > CurrentSensor::VALVE_DELTA_THRESHOLD_MA
            ? "COIL OK"
            : "NO CURRENT";
    char buf[96];
    snprintf_P(buf, sizeof(buf),
               PSTR("Wakeup plant %d: %u cycles, coil %dmA -> %dmA (%s)"), id,
               run.cycles, run.firstDelta, run.lastDelta, status);
    return String(buf);
  }

  // Проверяет подключён ли клапан id через дельту тока на ACS712.
  // Алгоритм откалиброван по экспериментам прогонов 1-3
  // (см. tasks/2026-05-24_valve_detect_pattern.md).
  //
  // ВАЖНО: перед первым вызовом в boot-цикле должен быть выполнен
  // primeAcsLineIfNeeded() — иначе первый замер словит «фоновый +18А»
  // от первого turnOnValve и даст false positive.
  bool checkValveConnected(int id, AwLogging& logger) {
    const int ON_DELAY_MS = 50;
    const int BASE_SAMPLES = 10;
    const int ACTIVE_SAMPLES = 5;

    int base = currentSensor.measureDcAvg(BASE_SAMPLES);
    valves.turnOn(id, logger);
    delay(ON_DELAY_MS);
    int active = currentSensor.measureDcAvg(ACTIVE_SAMPLES);
    valves.turnOff(id, logger);
    int delta = active - base;
    bool connected = delta > CurrentSensor::VALVE_DELTA_THRESHOLD_MA;
    logger.writeln((String)F("Valve ") + id + F(" check: delta=") + delta +
                   F("mA -> ") + (connected ? F("OK") : F("DISCONNECTED")));
    return connected;
  }

  // Проверяет все активные клапаны.
  // Возвращает строку для отправки в Telegram вида:
  //   "Valve check OK: plant0 OK, plant5 OK"
  //   "Valve check FAIL: plant0 OK, plant1 FAIL, plant2 FAIL"
  String checkAllActiveValves(State& state, AwLogging& logger) {
    primeAcsLineIfNeeded(logger);
    String details;
    bool anyFailed = false;
    bool first = true;
    for (int i = 0; i < PLANTS_AMOUNT; i++) {
      if (state.plants[i].isOn != PLANT_IS_ON) continue;
      wdt_reset();
      bool ok = checkValveConnected(i, logger);
      if (!ok) anyFailed = true;
      if (!first) details += F(", ");
      first = false;
      details += (String)F("plant") + i + (ok ? F(" OK") : F(" FAIL"));
    }
    if (first) return F("Valve check: no active plants");
    String result = anyFailed ? F("Valve check FAIL: ") : F("Valve check OK: ");
    result += details;
    return result;
  }
};

#endif  // POMP_H
