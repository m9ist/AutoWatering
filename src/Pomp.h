#include <ACS712.h>
#include <AwLogging.h>
#include <FlowSensor.h>
#include <State.h>
#include <avr/wdt.h>

#define PIN_REGISTER_CS 36   // stcp
#define PIN_REGISTER_DAT 34  // ds
#define PIN_REGISTER_CLK 38  // shcp

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

#define PIN_WATER_FLOW_SENSOR 2
#define WATER_FLOW_ITERATION_MS 100

#define PIN_AMPERAGE_SENSOR A2

// YFS401 было 2813
FlowSensor flowSensor = FlowSensor(YFS401, PIN_WATER_FLOW_SENSOR);
void waterFlowCount() { flowSensor.count(); }

class Pomp {
 private:
  bool pompState = false;
  uint32_t currentState = 0;
  int plantsToButton[PLANTS_AMOUNT] = {1, 3, 5, 7, 8, 10, 12, 14,
                                       0, 2, 4, 6, 9, 11, 13, 15};
  unsigned long timeCheck;
  unsigned long pompLoopStart;
  unsigned long pompLoopNextCheck;

  int currentPomp;
  bool acsPrimed = false;

  // Порог дельты тока, при которой считаем клапан подключённым.
  // Используется и в checkValveConnected (разовая проверка), и в
  // buildWaterReport (расшифровка статуса в Telegram-отчёте о поливе).
  static const int VALVE_DELTA_THRESHOLD_MA = 50;

  // статистика тока во время одного цикла полива
  long wateringAmpSum = 0;
  int wateringAmpCount = 0;
  int wateringAmpBaseline = 0;
  unsigned long wateringAmpLastSample = 0;

  // датчик тока
  ACS712 ACS = ACS712(PIN_AMPERAGE_SENSOR);

  // N замеров mA_DC с усреднением, ~10ms * n
  int measureDcAvg(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) {
      sum += ACS.mA_DC();
      delay(10);
    }
    return (int)(sum / n);
  }

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

  // --------- блок с клапанами

  void turnOnValve(int id, AwLogging& logger) {
    String out = F("ON valve");
    out += id;
    logger.writeln(out);
    bitWrite(currentState, patchValveId(id), HIGH);
    sendNewStateValves(currentState);
  }

  void turnOffValve(int id, AwLogging& logger) {
    String out = F("OFF valve");
    out += id;
    logger.writeln(out);
    bitWrite(currentState, patchValveId(id), LOW);
    sendNewStateValves(currentState);
  }

  // На плате клапана расположены по другому, по 7 на сдвиговый регистр, при чем
  // с 1 по 8 ножки, поэтому 0 - это 1, 6 - это 7, 7 - это 9, 14 - это 17
  int patchValveId(int id) {
    if (id < 7) {
      return id + 1;
    } else if (id < 14) {
      return id + 2;
    } else {
      return id + 3;
    }
  }

  // мы можем включать/выключать нужное кол-во клапанов через эту функцию
  void sendNewStateValves(uint32_t state) {
    // Устанавливаем 1 в соответствующий бит
    // 16 бит необходимо разделить на два байта:
    // И записать каждый байт в соответствующий регистр
    byte register1 = lowByte(state);
    state = state >> 8;
    byte register2 = lowByte(state);
    state = state >> 8;
    byte register3 = lowByte(state);

    digitalWrite(PIN_REGISTER_CS, LOW);

    // Последовательная передача данных на пин DS
    shiftOut(PIN_REGISTER_DAT, PIN_REGISTER_CLK, MSBFIRST, register3);
    shiftOut(PIN_REGISTER_DAT, PIN_REGISTER_CLK, MSBFIRST, register2);
    shiftOut(PIN_REGISTER_DAT, PIN_REGISTER_CLK, MSBFIRST, register1);

    digitalWrite(PIN_REGISTER_CS, HIGH);
  }

 public:
  Pomp(/* args */) {};
  ~Pomp() {};

  void initPomp(AwLogging& logger) {
    pinMode(PIN_POMP_MAIN, OUTPUT);
    pinMode(PIN_POMP_SPARE, OUTPUT);

    pinMode(PIN_REGISTER_CS, OUTPUT);
    pinMode(PIN_REGISTER_DAT, OUTPUT);
    pinMode(PIN_REGISTER_CLK, OUTPUT);

    pinMode(PIN_PLANT_MULTIPLEXER_S0, OUTPUT);
    pinMode(PIN_PLANT_MULTIPLEXER_S1, OUTPUT);
    pinMode(PIN_PLANT_MULTIPLEXER_S2, OUTPUT);
    pinMode(PIN_PLANT_MULTIPLEXER_S3, OUTPUT);

    pinMode(PIN_MULTIPLEXER_PLANT_TURN_ON_SIG, INPUT_PULLUP);
    pinMode(PIN_MULTIPLEXER_WATER_NOW_SIG, INPUT_PULLUP);

    pinMode(PIN_POMP_TURN_ON, INPUT_PULLUP);

    // работа с датчиком кол-ва воды
    uint8_t intSensor = digitalPinToInterrupt(PIN_WATER_FLOW_SENSOR);
    if (intSensor < 0) {
      // todo добавить в ошибки
      logger.writeln(F("!!!!!!!!!!!!!Указан вывод без EXT INT"));
    }
    flowSensor.begin(waterFlowCount);
    ACS.autoMidPoint(100, 5);  // калибровка без нагрузки
    logger.writeln((String)F("ACS midpoint: ") + ACS.getMidPoint());
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
    turnOnValve(id, logger);
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
    turnOffValve(id, logger);
    return millis() - timeCheck;
  }

  void beforeLoopFlowSensor() {
    flowSensor.read();
    flowSensor.resetVolume();
    flowSensor.resetPulse();
    pompLoopStart = millis();
    // считаем, что поток воды будет какое-то время разгоняться
    pompLoopNextCheck = pompLoopStart + 100;
  }

  void loopFlowSensor() {
    if (millis() < pompLoopStart) {
      // реализация в библиотеке не готова к этому, мы можем только обнулить
      beforeLoopFlowSensor();
      return;
    }
    if (millis() < pompLoopNextCheck) {
      return;
    }
    if (millis() - pompLoopStart < 400) {
      pompLoopNextCheck = millis() + 100;
    } else {
      pompLoopNextCheck = millis() + 1000;
    }
    flowSensor.read();
  }

  float getWaterFlowSensorMl() {
    flowSensor.read();
    return flowSensor.getVolume() * 1000;
  }

  // Сброс перед поливом. Подтягивает фоновый ток (primer) и снимает
  // baseline, относительно которого считается дельта в отчёте.
  void beginWateringAmpStats(AwLogging& logger) {
    primeAcsLineIfNeeded(logger);
    wateringAmpBaseline = measureDcAvg(10);
    logger.writeln((String)F("Water amp baseline: ") + wateringAmpBaseline +
                   F("mA"));
    wateringAmpSum = 0;
    wateringAmpCount = 0;
    wateringAmpLastSample = millis();
  }

  // Вызывать на каждой итерации цикла полива (как auto, так и manual).
  // Раз в AMP_SAMPLE_INTERVAL_MS снимает mA_DC, пишет в лог сырое
  // значение и дельту от baseline, копит сумму.
  // Сэмплинг по millis — корректно работает и при busy-wait цикле в
  // waterPlant, и в быстром while ручного полива.
  void sampleWateringAmpIfNeeded(AwLogging& logger) {
    const unsigned long AMP_SAMPLE_INTERVAL_MS = 500;
    if (millis() - wateringAmpLastSample < AMP_SAMPLE_INTERVAL_MS) return;
    wateringAmpLastSample = millis();
    int mA = ACS.mA_DC();
    logger.writeln((String)F("Water amp: ") + mA + F("mA (delta ") +
                   (mA - wateringAmpBaseline) + F("mA)"));
    wateringAmpSum += mA;
    wateringAmpCount++;
  }

  // Возвращает среднюю дельту тока относительно baseline.
  // Положительная — линия тянула больше baseline во время полива.
  int getWateringAmpDelta() {
    if (wateringAmpCount == 0) return 0;
    int avg = (int)(wateringAmpSum / wateringAmpCount);
    return avg - wateringAmpBaseline;
  }

  // Собирает унифицированную строку отчёта о поливе.
  // requestedMl < 0 — manual полив без заданного объёма.
  // ampDelta — средний ток поверх baseline (см. getWateringAmpDelta).
  String buildWaterReport(int id, int requestedMl, unsigned long actualMs,
                          float realMl, int ampDelta) {
    String out = (String)F("Done water id ") + id;
    if (requestedMl >= 0) {
      out += (String)F(" with ") + requestedMl + F("ml");
    }
    out += (String)F(". Amperage delta: ") + ampDelta + F("mA (") +
           (ampDelta > VALVE_DELTA_THRESHOLD_MA ? F("OK") : F("DISCONNECTED")) +
           F(")");
    out += (String)F(". Duration ") + actualMs + F("ms");
    out += (String)F(". Real ml = ") + realMl;
    return out;
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

    int base = measureDcAvg(BASE_SAMPLES);
    turnOnValve(id, logger);
    delay(ON_DELAY_MS);
    int active = measureDcAvg(ACTIVE_SAMPLES);
    turnOffValve(id, logger);
    int delta = active - base;
    bool connected = delta > VALVE_DELTA_THRESHOLD_MA;
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

 private:
  // Холостой ON-OFF одного клапана — поднимает «фоновый ток» в линии
  // до стабильного уровня. Делается один раз за boot.
  // Подробности эффекта — см. tasks/2026-05-24_valve_detect_pattern.md.
  void primeAcsLineIfNeeded(AwLogging& logger) {
    if (acsPrimed) return;
    const int PRIMER_SLOT = 0;
    const int PRIMER_ON_MS = 150;
    const int PRIMER_SETTLE_MS = 200;
    logger.writeln(F("ACS primer: warming up baseline"));
    turnOnValve(PRIMER_SLOT, logger);
    delay(PRIMER_ON_MS);
    turnOffValve(PRIMER_SLOT, logger);
    delay(PRIMER_SETTLE_MS);
    acsPrimed = true;
  }
};
