#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H
#include <ACS712.h>
#include <Arduino.h>
#include <AwLogging.h>

#define PIN_AMPERAGE_SENSOR A2

// Датчик тока ACS712: диагностика подключения клапанов по дельте тока
// и статистика тока во время полива.
class CurrentSensor {
 private:
  ACS712 ACS = ACS712(PIN_AMPERAGE_SENSOR);

  // статистика тока во время одного цикла полива
  long wateringAmpSum = 0;
  int wateringAmpCount = 0;
  int wateringAmpBaseline = 0;
  unsigned long wateringAmpLastSample = 0;

 public:
  // Порог дельты тока, при которой считаем клапан подключённым.
  // Используется и в checkValveConnected (разовая проверка), и в
  // buildWaterReport (расшифровка статуса в Telegram-отчёте о поливе).
  static const int VALVE_DELTA_THRESHOLD_MA = 50;

  void init(AwLogging& logger) {
    ACS.autoMidPoint(100, 5);  // калибровка без нагрузки
    logger.writeln((String)F("ACS midpoint: ") + ACS.getMidPoint());
  }

  // N замеров mA_DC с усреднением, ~10ms * n
  int measureDcAvg(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) {
      sum += ACS.mA_DC();
      delay(10);
    }
    return (int)(sum / n);
  }

  // Сброс перед поливом. Снимает baseline, относительно которого
  // считается дельта в отчёте. Прогрев линии (primer) — на стороне Pomp.
  void beginWateringStats(AwLogging& logger) {
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
  void sampleIfNeeded(AwLogging& logger) {
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
  int getWateringDelta() {
    if (wateringAmpCount == 0) return 0;
    int avg = (int)(wateringAmpSum / wateringAmpCount);
    return avg - wateringAmpBaseline;
  }
};

#endif  // CURRENT_SENSOR_H
