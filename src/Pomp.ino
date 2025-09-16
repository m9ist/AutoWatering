
int pompPin1 = 24;
int pompPin2 = 25;
bool pompState = false;

void initPomp() {
  pinMode(pompPin1, OUTPUT);
  pinMode(pompPin2, OUTPUT);
  return;
}

void startPump() {
  writeln("Start pomp");
  digitalWrite(pompPin1, LOW);
  digitalWrite(pompPin2, HIGH);
}

void stopPump() {
  writeln("Stop pomp");
  digitalWrite(pompPin1, LOW);
  digitalWrite(pompPin2, LOW);
}

void pumpLoop() {
  startPump();
  delay(1000);
  stopPump();
  delay(4000);
}