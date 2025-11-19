#include <AwLogging.h>
#include <State.h>

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

#define PIN_POMP 35
#define PIN_POMP_TURN_ON 17

class Pomp {
 private:
  bool pompState = false;
  uint32_t currentState = 0;
  int plantsToButton[PLANTS_AMOUNT] = {1, 3, 5, 7, 8, 10, 12, 14,
                                       0, 2, 4, 6, 9, 11, 13, 15};
  unsigned long timeCheck;

  void startPomp(AwLogging& logger) {
    logger.writeln(F("Start pomp"));
    digitalWrite(PIN_POMP, HIGH);
  }

  void stopPomp(AwLogging& logger) {
    logger.writeln(F("Stop pomp"));
    digitalWrite(PIN_POMP, LOW);
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

  void initPomp() {
    pinMode(PIN_POMP, OUTPUT);

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
  }

  // обновляет включено ли юзером растение на тумблере
  // true - если было какое-то изменение
  bool updatePlantsState(State& state) {
    bool v = digitalRead(PIN_POMP_TURN_ON) != HIGH;
    if (v) {
      if (!state.pompIsOn) {
        state.pompIsOn = true;
      }
    } else {
      if (state.pompIsOn) {
        state.pompIsOn = false;
      }
    }
    bool wasUpdate = false;
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
        if (state.plants[i].isOn == PLANT_IS_ON) {
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
    String out = "Watering plant ";
    out += id;
    timeCheck = millis();
    // drawScreenMessage(out, logger);
    // todo out String <<<<<<<<<<<<<<<<<<<<<<<<<<<<<

    turnOnValve(id, logger);
    startPomp(logger);
  }

  String stopWaterPlant(int id, AwLogging& logger) {
    stopPomp(logger);
    turnOffValve(id, logger);

    timeCheck = millis() - timeCheck;
    String out =
        (String)F("End watering plant ") + id + F(" in ") + timeCheck + F("ms");
    return out;
  }

  String waterPlant(int id, int amounMl) {
    return (String)F("Done task water plant ") + id + F(" with ") + amounMl +
           F("ml. Did nothing!!!");
  }
};
