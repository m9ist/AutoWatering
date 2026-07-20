#ifndef FLOW_METER_H
#define FLOW_METER_H
#include <Arduino.h>
#include <AwLogging.h>
#include <FlowSensor.h>

#define PIN_WATER_FLOW_SENSOR 2

// YFS401 было 2813
FlowSensor flowSensor = FlowSensor(YFS401, PIN_WATER_FLOW_SENSOR);
void waterFlowCount() { flowSensor.count(); }

// Датчик расхода воды YF-S401. Используется как контроль «полив прошёл
// успешно» (Real ml в отчёте), дозирование по нему не идёт — датчик не
// откалиброван, а система нестабильна.
class FlowMeter {
 private:
  unsigned long loopStart = 0;
  unsigned long loopNextCheck = 0;

 public:
  void init(AwLogging& logger) {
    uint8_t intSensor = digitalPinToInterrupt(PIN_WATER_FLOW_SENSOR);
    if (intSensor < 0) {
      // todo добавить в ошибки
      logger.writeln(F("!!!!!!!!!!!!!Указан вывод без EXT INT"));
    }
    flowSensor.begin(waterFlowCount);
  }

  void beforeLoop() {
    flowSensor.read();
    flowSensor.resetVolume();
    flowSensor.resetPulse();
    loopStart = millis();
    // считаем, что поток воды будет какое-то время разгоняться
    loopNextCheck = loopStart + 100;
  }

  void loop() {
    if (millis() < loopStart) {
      // реализация в библиотеке не готова к этому, мы можем только обнулить
      beforeLoop();
      return;
    }
    if (millis() < loopNextCheck) {
      return;
    }
    if (millis() - loopStart < 400) {
      loopNextCheck = millis() + 100;
    } else {
      loopNextCheck = millis() + 1000;
    }
    flowSensor.read();
  }

  float getMl() {
    flowSensor.read();
    return flowSensor.getVolume() * 1000;
  }
};

#endif  // FLOW_METER_H
