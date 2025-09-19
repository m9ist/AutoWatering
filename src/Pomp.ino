
int pompPin1 = 37;
bool pompState = false;

void initPomp() {
  pinMode(pompPin1, OUTPUT);
  return;
}

void startPump() {
  writeln("Start pomp");
  digitalWrite(pompPin1, HIGH);
}

void stopPump() {
  writeln("Stop pomp");
  digitalWrite(pompPin1, LOW);
}

uint32_t pompVarTime = 0;
int nextTime = 5000;
bool turnedOn = false;

void pumpLoop() {
  if ((pompVarTime + nextTime) < millis() || pompVarTime > millis()) {
    // Сохраняем  время последних вычислений.
    pompVarTime = millis();
    if (turnedOn) {
      stopPump();
      turnedOn = false;
      nextTime = 5000;
    } else {
      startPump();
      turnedOn = true;
      nextTime = 1000;
    }
  }
}