
// Объявляем переменную для хранения времени последнего расчёта.
uint32_t varTime = 0;
// Объявляем переменную для хранения рассчитанной скорости потока воды (л/с).
float varQ = 0;
// Объявляем переменную для хранения рассчитанного объема воды (л).
float varV = 0;
// Объявляем переменную для хранения частоты импульсов (Гц).
volatile uint16_t varF = 0;

void funCountInt() { varF++; }

void initSensors() {
  writeln("Start init sensors");

  int pinSensor = 18;
  // работа с датчиком кол-ва воды
  pinMode(pinSensor, INPUT);
  uint8_t intSensor = digitalPinToInterrupt(pinSensor);
  attachInterrupt(intSensor, funCountInt, RISING);
  if (intSensor < 0) {
    Serial.print("Указан вывод без EXT INT");
  }

  if (true) return;     // схема с мультиплексором
  pinMode(47, OUTPUT);  // s0
  pinMode(45, OUTPUT);  // s1
  pinMode(43, OUTPUT);
  pinMode(41, OUTPUT);

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
    varQ = varTmp / ((float)varTmp * 5.9f + 4570.0f); //todo добавить нормировку по милисекундам
    // Сохраняем  время последних вычислений.
    varTime = millis();
    // Определяем объем воды л.
    varV += varQ;
    //  Выводим рассчитанные данные:
    Serial.println((String) "Объем " + varV + "л, скорость " + (varQ * 60.0f) +
                   "л/м.");
  }
  if (true) return;  // схема с мультиплексором
  for (int i = 15; i >= 0; i--) {
    Serial.print("i=");
    Serial.print(i);
    Serial.print("; bits=");
    int s0 = i % 2;
    int s1 = (i / 2) % 2;
    int s2 = (i / 4) % 2;
    int s3 = (i / 8) % 2;
    Serial.print(s0);
    Serial.print(s1);
    Serial.print(s2);
    Serial.print(s3);
    Serial.print(" ... read value ... ");
    // включаем пин 1
    digitalWrite(47, s0 == 0 ? LOW : HIGH);
    digitalWrite(45, s1 == 0 ? LOW : HIGH);
    digitalWrite(43, s2 == 0 ? LOW : HIGH);
    digitalWrite(41, s3 == 0 ? LOW : HIGH);
    delay(100);
    long val = 0;
    for (int i = 0; i < measure_time; i++) {
      int read = analogRead(15);
      val += read;
      if (val < 100) {
        Serial.print(" ");
      }
      Serial.print(read);
      Serial.print(".");
      delay(10);
    }

    Serial.println(val / measure_time);
  }

  delay(10000);

  // delay(1000);  // !!!!todo сменить на более "боевой" таймаут
  // loopId++;
  // int data3 = analogRead(1);
  // // map(data, 579, 0, 0, 100)
  // String s = "Loop number;";
  // s += loopId;
  // s += ";s1;-1; s2;-1;s3;";
  // s += data3;
  // writeln(s);
}