#include <SHT31_SWW.h>

#define PIN_SERNSOR_S0 26
#define PIN_SERNSOR_S1 28
#define PIN_SERNSOR_S2 30
#define PIN_SERNSOR_S3 32

#define PIN_UNPUT_SENSOR 0

#define PIN_TEMP_SDA 19
#define PINT_TEMP_SCL 21

// Объявляем переменную для хранения времени последнего расчёта.
uint32_t varTime = 0;
// Объявляем переменную для хранения рассчитанной скорости потока воды (л/с).
float varQ = 0;
// Объявляем переменную для хранения рассчитанного объема воды (л).
float varV = 0;
// Объявляем переменную для хранения частоты импульсов (Гц).
volatile uint16_t varF = 0;

SoftwareWire wireTempSensor(PIN_TEMP_SDA, PINT_TEMP_SCL);
SHT31_SWW sht31(0x44, &wireTempSensor);

void funCountInt() { varF++; }

void initSensors() {
  writeln("Start init sensors");

  if (false) {
    // работа с датчиком влажности и температуры
    wireTempSensor.begin();
    bool b = sht31.begin();
    Serial.print("SHT31 connection: ");
    Serial.println(b);
  }

  if (false) {
    // работа с датчиком кол-ва воды
    int pinSensor = 18;
    pinMode(pinSensor, INPUT);
    uint8_t intSensor = digitalPinToInterrupt(pinSensor);
    attachInterrupt(intSensor, funCountInt, RISING);
    if (intSensor < 0) {
      Serial.print("Указан вывод без EXT INT");
    }
  }

  // схема с мультиплексором
  if (true) {
    pinMode(PIN_SERNSOR_S0, OUTPUT);  // s0
    pinMode(PIN_SERNSOR_S1, OUTPUT);  // s1
    pinMode(PIN_SERNSOR_S2, OUTPUT);
    pinMode(PIN_SERNSOR_S3, OUTPUT);
    writeln(
        "event  ;1   ;2   ;3   ;4   ;5   ;6   ;7   ;8   ;9   ;10  ;11  ;12  "
        ";13  ;14  ;15  ;16 ;temp;hum;  date    ;time");
  }

  // 1 датчик на 1 выходе - через плату HW-080 ... 410 показания неподключенные
  // 1023- проблемы с землей 2 датчик на 4 выходе - v 1.2 3 датчик на 7 выходе -
  // TDR 4 датчик - нужен резистор !!!!!!!!!! туду

  // туду проверить без питания и без подключения к порту

  // пины прерывания int0 2 int1 3 int2 21 int3 20 int4 19 int5 18
  // все digital пины поддерживают прерывания через
  // https://github.com/NicoHood/PinChangeInterrupt помечать данные volatile,
  // если надо совмещать данные программы и прерывания безопасное чтение
  // многобайтовых переменных через отключение прерываний...     noInterrupts();
  // ... interrupts();
  //
}

int loopId = 0;

int measure_time = 10;

void loopSensors() {
  if (false) {
    sht31.read(false);
    float temp = sht31.getTemperature();
    float hum = sht31.getHumidity();
    Serial.print("t = ");
    Serial.print(temp);
    Serial.print(", h = ");
    Serial.println(hum);
    delay(3000);
  }
  if (false) {
    //   Если прошла 1 секунда:
    // Если c момента последнего расчёта прошла 1 секунда,
    //  Определяем скорость и расход воды:
    // или произошло переполнение millis то ...
    if ((varTime + 1000) < millis() || varTime > millis()) {
      // Сбрасываем частоту импульсов датчика, значение этой переменной
      // приращается по прерываниям.
      uint32_t varTmp = varF;
      varF = 0;

      // Определяем скорость потока воды л/с.
      varQ = varTmp / ((float)varTmp * 5.9f +
                       4570.0f);  // todo добавить нормировку по милисекундам
      // Сохраняем  время последних вычислений.
      varTime = millis();
      // Определяем объем воды л.
      varV += varQ;
      //  Выводим рассчитанные данные:
      Serial.println((String) "Объем " + varV + "л, скорость " +
                     (varQ * 60.0f) + "л/м.");
    }
  }
  if (true) {
    String out = "sensors;";
    for (int i = 0; i < 16; i++) {
      digitalWrite(PIN_SERNSOR_S0, bitRead(i, 0));
      digitalWrite(PIN_SERNSOR_S1, bitRead(i, 1));
      digitalWrite(PIN_SERNSOR_S2, bitRead(i, 2));
      digitalWrite(PIN_SERNSOR_S3, bitRead(i, 3));
      // delay(50);
      int read = analogRead(PIN_UNPUT_SENSOR);
      out += read;
      global_state.plants[i].parrots =
          constrain(map(read, 950, 350, 0, 100), 0, 99);
      out += ";";
      // Serial.print(read);
      // Serial.print(";");
    }
    sht31.read(false);
    global_state.temperature = sht31.getTemperature();
    global_state.humidity = sht31.getHumidity();
    out += global_state.temperature;
    out += ";";
    out += global_state.humidity;
    out += ";";
    out += dateToString(getNow());
    writeln(out);

    // int data3 = analogRead(1);
    // // map(data, 579, 0, 0, 100)
    // String s = "Loop number;";
    // s += loopId;
    // s += ";s1;-1; s2;-1;s3;";
    // s += data3;
    // writeln(s);
  }
}

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