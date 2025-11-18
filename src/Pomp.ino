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

  pinMode(PIN_POMP_TURN_ON, INPUT_PULLUP);
}

void multiplexPlant(int id) {
  digitalWrite(PIN_PLANT_MULTIPLEXER_S0, bitRead(id, 0));
  digitalWrite(PIN_PLANT_MULTIPLEXER_S1, bitRead(id, 1));
  digitalWrite(PIN_PLANT_MULTIPLEXER_S2, bitRead(id, 2));
  digitalWrite(PIN_PLANT_MULTIPLEXER_S3, bitRead(id, 3));
}

// обновляет включено ли юзером растение на тумблере
void updatePlantsState(bool needStateUpdate) {
  bool v = digitalRead(PIN_POMP_TURN_ON) != HIGH;
  if (v) {
    if (!global_state.pompIsOn) {
      global_state.pompIsOn = true;
    }
  } else {
    if (global_state.pompIsOn) {
      global_state.pompIsOn = false;
    }
  }
  bool wasUpdate = false;
  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    multiplexPlant(i);
    v = digitalRead(PIN_MULTIPLEXER_PLANT_TURN_ON_SIG) != HIGH;
    if (v) {
      if (global_state.plants[i].isOn == PLANT_IS_OFF_USER ||
          global_state.plants[i].isOn == PLANT_IS_UNDEFINED) {
        global_state.plants[i].isOn = PLANT_IS_ON;
        wasUpdate = true;
      }
    } else {
      if (global_state.plants[i].isOn == PLANT_IS_ON) {
        global_state.plants[i].isOn = PLANT_IS_OFF_USER;
        wasUpdate = true;
      }
    }
  }
  if (wasUpdate && needStateUpdate) stateUpdated();
}

bool isWaterNowButtonPressed(int id) {
  int pinI = plantsToButton[id];
  multiplexPlant(pinI);
  int v = digitalRead(PIN_MULTIPLEXER_WATER_NOW_SIG);
  return v == LOW;
}

//---------- блок с помпой

void startPomp() {
  writeln(F("Start pomp"));
  digitalWrite(PIN_POMP, HIGH);
}

void stopPomp() {
  writeln(F("Stop pomp"));
  digitalWrite(PIN_POMP, LOW);
}

unsigned long timeCheck;

void startWaterPlant(int id) {
  String out = "Watering plant ";
  out += id;
  timeCheck = millis();
  drawScreenMessage(out);

  turnOnValve(id);
  startPomp();
}

//*
String stopWaterPlant(int id) {
  stopPomp();
  turnOffValve(id);

  timeCheck = millis() - timeCheck;
  String out = (String) F("End watering plant ") + id + F(" in ") + timeCheck + F("ms");
  return out;
}

String waterPlant(int id, int amounMl) {
  return (String) F("Done task water plant ") + id + F(" with ") + amounMl +
         F("ml. Did nothing!!!");
}
//*/

// --------- блок с клапанами

void turnOnValve(int id) {
  String out = F("ON valve");
  out += id;
  writeln(out);
  bitWrite(currentState, patchValveId(id), HIGH);
  sendNewStateValves(currentState);
}

void turnOffValve(int id) {
  String out = F("OFF valve");
  out += id;
  writeln(out);
  bitWrite(currentState, patchValveId(id), LOW);
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
