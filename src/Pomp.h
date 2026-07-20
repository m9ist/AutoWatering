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

#define WATER_FLOW_ITERATION_MS 100

// Оркестратор полива: насос, кнопки/тумблеры через мультиплексеры и
// сценарии полива. Клапаны, расходомер и датчик тока — отдельные
// модули (Valves, FlowMeter, CurrentSensor), Pomp их координирует.
class Pomp {
 private:
  bool pompState = false;
  int plantsToButton[PLANTS_AMOUNT] = {1, 3, 5, 7, 8, 10, 12, 14,
                                       0, 2, 4, 6, 9, 11, 13, 15};
  unsigned long timeCheck;

  int currentPomp;
  bool acsPrimed = false;

  Valves valves;
  FlowMeter flowMeter;
  CurrentSensor currentSensor;

  void startPomp(AwLogging& logger) {
    logger.writeln(F("Start pomp"));
    // todo запрогать схему со сменой моторов
    currentPomp = PIN_POMP_SPARE;
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

  void startWaterPlant(int id, AwLogging& logger) {
    logger.writeln((String)F("Watering plant ") + id);
    timeCheck = millis();
    valves.turnOn(id, logger);
    // сделано, чтобы не создавать напряжение на клапанах
    delay(200);
    startPomp(logger);
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
  // snprintf в статический буфер вместо конкатенации String — меньше
  // реаллокаций кучи (фрагментация на 8КБ RAM).
  String buildWaterReport(int id, int requestedMl, unsigned long actualMs,
                          float realMl, int ampDelta) {
    const char* valveStatus =
        ampDelta > CurrentSensor::VALVE_DELTA_THRESHOLD_MA ? "OK"
                                                           : "DISCONNECTED";
    char buf[140];
    if (requestedMl >= 0) {
      snprintf_P(buf, sizeof(buf),
                 PSTR("Done water id %d with %dml. Amperage delta: %dmA (%s). "
                      "Duration %lums. Real ml = %d"),
                 id, requestedMl, ampDelta, valveStatus, actualMs, (int)realMl);
    } else {
      snprintf_P(buf, sizeof(buf),
                 PSTR("Done water id %d. Amperage delta: %dmA (%s). "
                      "Duration %lums. Real ml = %d"),
                 id, ampDelta, valveStatus, actualMs, (int)realMl);
    }
    return String(buf);
  }

  String waterPlant(int id, int amounMl, AwLogging& logger) {
    wdt_reset();
    float practicalSpeedMlInMs = 0.0077;
    // сколько итераций по 0.1 сек нужно сделать
    int expectedNumIterations =
        (float)amounMl / practicalSpeedMlInMs / WATER_FLOW_ITERATION_MS;
    logger.writeln((String)F("Num iterations = ") + expectedNumIterations);

    beginWateringAmpStats(logger);
    beforeLoopFlowSensor();
    unsigned long start = millis();
    startWaterPlant(id, logger);

    for (int iter = 0; iter < expectedNumIterations; iter++) {
      while (start + WATER_FLOW_ITERATION_MS > millis() && start <= millis()) {
      }
      loopFlowSensor();
      sampleWateringAmpIfNeeded(logger);
      wdt_reset();
      start = millis();
    }

    unsigned long actualMs = stopWaterPlant(id, logger);
    return buildWaterReport(id, amounMl, actualMs, getWaterFlowSensorMl(),
                            getWateringAmpDelta());
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
