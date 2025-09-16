
void initSensors() {
  writeln("Start init sensors");
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

void loopSensors() {
  delay(1000);  // !!!!todo сменить на более "боевой" таймаут
  loopId++;
  int data3 = analogRead(1);
  // map(data, 579, 0, 0, 100)
  String s = "Loop number;";
  s += loopId;
  s += ";s1;-1; s2;-1;s3;";
  s += data3;
  writeln(s);
}