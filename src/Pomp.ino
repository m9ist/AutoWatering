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

bool pompState = false;
uint32_t currentState = 0;
int plantsToButton[] = {1, 3, 5, 7, 8, 10, 12, 14, 0, 2, 4, 6, 9, 11, 13, 15};

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
}

//---------- блок с кнопками принуд пролива и тумблером включения полива
// растения
void updatePlantsState() {
  String log = "Plants turned changed";
  bool needPrint = false;
  for (int i = 0; i < 16; i++) {
    log += ';';
    log += i;
    log += '=';
    digitalWrite(PIN_PLANT_MULTIPLEXER_S0, bitRead(i, 0));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S1, bitRead(i, 1));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S2, bitRead(i, 2));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S3, bitRead(i, 3));
    bool v = digitalRead(PIN_MULTIPLEXER_PLANT_TURN_ON_SIG) != HIGH;
    if (v) {
      if (global_state.plants[i].isOn == PLANT_IS_OFF_USER) {
        global_state.plants[i].isOn = PLANT_IS_ON;
        needPrint = true;
      }
    } else {
      if (global_state.plants[i].isOn == PLANT_IS_ON) {
        global_state.plants[i].isOn = PLANT_IS_OFF_USER;
        needPrint = true;
      }
    }
    log += v ? '0' : '_';
  }
  if (needPrint) writeln(log);
}

void loopMultuplexer() {
  Serial.print("Plants turned  ");
  for (int i = 0; i < 16; i++) {
    Serial.print(";");
    Serial.print(i);
    Serial.print("=");
    digitalWrite(PIN_PLANT_MULTIPLEXER_S0, bitRead(i, 0));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S1, bitRead(i, 1));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S2, bitRead(i, 2));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S3, bitRead(i, 3));
    int v = digitalRead(PIN_MULTIPLEXER_PLANT_TURN_ON_SIG);
    Serial.print(v ? "_" : "0");
  }
  Serial.println();
  Serial.print("Water plant now");
  for (int i = 0; i < 16; i++) {
    Serial.print(";");
    Serial.print(i);
    int pinI = plantsToButton[i];
    Serial.print("=");
    digitalWrite(PIN_PLANT_MULTIPLEXER_S0, bitRead(pinI, 0));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S1, bitRead(pinI, 1));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S2, bitRead(pinI, 2));
    digitalWrite(PIN_PLANT_MULTIPLEXER_S3, bitRead(pinI, 3));
    int v = digitalRead(PIN_MULTIPLEXER_WATER_NOW_SIG);
    Serial.print(v ? "_" : "0");
  }
  Serial.println();
}

bool isWaterNowButtonPressed(int id) {
  int pinI = plantsToButton[id];
  digitalWrite(PIN_PLANT_MULTIPLEXER_S0, bitRead(pinI, 0));
  digitalWrite(PIN_PLANT_MULTIPLEXER_S1, bitRead(pinI, 1));
  digitalWrite(PIN_PLANT_MULTIPLEXER_S2, bitRead(pinI, 2));
  digitalWrite(PIN_PLANT_MULTIPLEXER_S3, bitRead(pinI, 3));
  int v = digitalRead(PIN_MULTIPLEXER_WATER_NOW_SIG);
  return v == LOW;
}

//---------- блок с помпой

void startPomp() {
  writeln("Start pomp");
  digitalWrite(PIN_POMP, HIGH);
}

void stopPomp() {
  writeln("Stop pomp");
  digitalWrite(PIN_POMP, LOW);
}

// --------- блок с клапанами

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