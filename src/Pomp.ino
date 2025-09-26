#define PIN_REGISTER_CS 36   // stcp
#define PIN_REGISTER_DAT 34  // ds
#define PIN_REGISTER_CLK 38  // shcp

#define PIN_POMP -1

bool pompState = false;
uint32_t currentState = 0;

void initPomp() {
  // pinMode(PIN_POMP, OUTPUT);
  pinMode(PIN_REGISTER_CS, OUTPUT);
  pinMode(PIN_REGISTER_DAT, OUTPUT);
  pinMode(PIN_REGISTER_CLK, OUTPUT);
}

void startPomp() {
  writeln("Start pomp");
  digitalWrite(PIN_POMP, HIGH);
}

void stopPomp() {
  writeln("Stop pomp");
  digitalWrite(PIN_POMP, LOW);
}

void turnOnValve(int id) {
  Serial.print("ON  valve ");
  Serial.print(id);
  Serial.print(" state = ");
  Serial.print(currentState);

  bitWrite(currentState, patchValveId(id), HIGH);
  Serial.print(", new state = ");
  Serial.print(currentState);

  sendNewStateValves(currentState);
}

void turnOffValve(int id) {
  Serial.print("OFF valve ");
  Serial.print(id);
  Serial.print(" state = ");
  Serial.print(currentState, BIN);

  bitWrite(currentState, patchValveId(id), LOW);
  Serial.print(", new state = ");
  Serial.print(currentState, BIN);

  sendNewStateValves(currentState);
}

// На плате клапана расположены по другому, по 7 на сдвиговый регистр, при чем с
// 1 по 8 ножки, поэтому 0 - это 1, 6 - это 7, 7 - это 9, 14 - это 17
int patchValveId(int id) {
  if (id < 7) {
    return id + 1;
  } else if (id < 14) {
    return id + 2;
  } else {
    return id + 3;
  }
}

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
  Serial.print("; send to shift registers ");
  Serial.print(register3);
  shiftOut(PIN_REGISTER_DAT, PIN_REGISTER_CLK, MSBFIRST, register3);
  Serial.print(" ");
  Serial.print(register2);
  shiftOut(PIN_REGISTER_DAT, PIN_REGISTER_CLK, MSBFIRST, register2);
  Serial.print(" ");
  Serial.println(register1);
  shiftOut(PIN_REGISTER_DAT, PIN_REGISTER_CLK, MSBFIRST, register1);

  digitalWrite(PIN_REGISTER_CS, HIGH);
}

uint32_t pompVarTime = 0;
int nextTime = 5000;
bool turnedOn = false;

void loopPomp() {
  for (int i = 0; i < 16; i++) {
    turnOnValve(i);
    delay(4000);
    turnOffValve(i);
    delay(1000);
  }
  Serial.println("---------------------");

  if (true) {
    return;
  }
  if ((pompVarTime + nextTime) < millis() || pompVarTime > millis()) {
    // Сохраняем  время последних вычислений.
    pompVarTime = millis();
    if (turnedOn) {
      stopPomp();
      turnedOn = false;
      nextTime = 5000;
    } else {
      startPomp();
      turnedOn = true;
      nextTime = 1000;
    }
  }
}