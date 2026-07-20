#ifndef VALVES_H
#define VALVES_H
#include <Arduino.h>
#include <AwLogging.h>

#define PIN_REGISTER_CS 36   // stcp
#define PIN_REGISTER_DAT 34  // ds
#define PIN_REGISTER_CLK 38  // shcp

// Управление 16 электромагнитными клапанами через каскад из трёх 74HC595
class Valves {
 private:
  uint32_t currentState = 0;

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
  void sendNewState(uint32_t state) {
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
  void init() {
    pinMode(PIN_REGISTER_CS, OUTPUT);
    pinMode(PIN_REGISTER_DAT, OUTPUT);
    pinMode(PIN_REGISTER_CLK, OUTPUT);
  }

  void turnOn(int id, AwLogging& logger) {
    String out = F("ON valve");
    out += id;
    logger.writeln(out);
    bitWrite(currentState, patchValveId(id), HIGH);
    sendNewState(currentState);
  }

  void turnOff(int id, AwLogging& logger) {
    String out = F("OFF valve");
    out += id;
    logger.writeln(out);
    bitWrite(currentState, patchValveId(id), LOW);
    sendNewState(currentState);
  }
};

#endif  // VALVES_H
