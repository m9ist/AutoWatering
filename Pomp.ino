int pompButton = 2;
int pompPin1 = 5;
int pompPin2 = 6;
bool pompState = false;

void initPomp() {
    pinMode(pompButton, INPUT_PULLUP);
    pinMode(pompPin1, OUTPUT);
    pinMode(pompPin2, OUTPUT);
    return;
}

void startPump() {
  writeln("Start pomp");
  digitalWrite(pompPin1, HIGH);
  digitalWrite(pompPin2, LOW);
}

void stopPump() {
  writeln("Stop pomp");
  digitalWrite(pompPin1, LOW);
  digitalWrite(pompPin2, HIGH);
}

void pumpLoop() {
  if (digitalRead(pompButton) == HIGH) {
      if (!pompState) {
        pompState = true;
        startPump();
      }
    } else {
      if (pompState) {
        pompState = false;
        stopPump();
      }
    }
}