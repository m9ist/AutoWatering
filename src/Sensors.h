#include <SHT31_SWW.h>

#include "AwLogging.h"
#include "State.h"

#define PIN_SERNSOR_S0 26
#define PIN_SERNSOR_S1 28
#define PIN_SERNSOR_S2 30
#define PIN_SERNSOR_S3 32

#define PIN_UNPUT_SENSOR 0

#define PIN_TEMP_SDA 19
#define PIN_TEMP_SCL 21

#define PIN_WATER_LEVEL A1

class Sensors {
 protected:
  SoftwareWire wireTempSensor = SoftwareWire(PIN_TEMP_SDA, PIN_TEMP_SCL);
  SHT31_SWW sht31 = SHT31_SWW(0x44, &wireTempSensor);

  void loopSoilMoistureSensors(State& state) {
    for (int i = 0; i < PLANTS_AMOUNT; i++) {
      digitalWrite(PIN_SERNSOR_S0, bitRead(i, 0));
      digitalWrite(PIN_SERNSOR_S1, bitRead(i, 1));
      digitalWrite(PIN_SERNSOR_S2, bitRead(i, 2));
      digitalWrite(PIN_SERNSOR_S3, bitRead(i, 3));
      // delay(50);
      int read = analogRead(PIN_UNPUT_SENSOR);
      state.plants[i].originalValue = read;
      state.plants[i].parrots = constrain(map(read, 950, 350, 0, 100), 0, 99);
    }
  }

 public:
  void init(AwLogging& logger, State& state) {
    logger.writeln(F("Start init sensors"));

    // работа с датчиком влажности и температуры
    wireTempSensor.begin();
    bool b = sht31.begin();
    logger.writeln((String)F("SHT31 connection: ") + b);
    state.temperatureSensorInited = b;

    // схема с мультиплексором для датчиков влажности почвы
    pinMode(PIN_SERNSOR_S0, OUTPUT);  // s0
    pinMode(PIN_SERNSOR_S1, OUTPUT);  // s1
    pinMode(PIN_SERNSOR_S2, OUTPUT);
    pinMode(PIN_SERNSOR_S3, OUTPUT);

    pinMode(PIN_WATER_LEVEL, INPUT);

    // 1 датчик на 1 выходе - через плату HW-080 ... 410 показания
    // неподключенные 1023- проблемы с землей 2 датчик на 4 выходе - v 1.2 3
    // датчик на 7 выходе - TDR 4 датчик - нужен резистор !!!!!!!!!! туду

    // туду проверить без питания и без подключения к порту

    // пины прерывания int0 2 int1 3 int2 21 int3 20 int4 19 int5 18
    // все digital пины поддерживают прерывания через
    // https://github.com/NicoHood/PinChangeInterrupt помечать данные volatile,
    // если надо совмещать данные программы и прерывания безопасное чтение
    // многобайтовых переменных через отключение прерываний... noInterrupts();
    // ... interrupts();
    //
  }

  void loopSensors(AwLogging& logger, State& state) {
    logger.writeln(F("Start loop sensors."));
    loopSoilMoistureSensors(state);
    sht31.read(false);
    state.temperature = sht31.getTemperature();
    state.humidity = sht31.getHumidity();

    int waterLevel = digitalRead(PIN_WATER_LEVEL);
    // state.hasWaterLevel = waterLevel == HIGH;
  }
};

/*
1023 - не подключен либо воздух
950-1013 - сухо
150-270 - в воде, начинается с 100 и "разгоняется", минуты 3 на устаканиться


полили 20г на "с ушками"
328;384;429;301 13:43:42
429;671;648;489 13:46:42
455;697;705;570 13:51:16
526;754;711;680 14:02:15
538;774;662;718 14:16:10
608;815;656;772 14:54:35
576;804;626;761 15:17:13
593;808;608;813 16:32:34
573;608;581;634 17:48:17
*/