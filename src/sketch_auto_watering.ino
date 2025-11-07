#include <Ds1302.h>
#include <State.h>
#include <Timer.h>

#define IS_DEBUG true

// Alt + Shift + F - автоформатирование кода
// Ctrl + Alt + B - компиляция
// Ctrl + Alt + U - загрузка прошивки

State global_state;

Timer timerSensorsCheck;
const Duration repeatIntervalSensorsCheck = Timer::Minutes(1);

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);
  writeln("Start working");
  initLogging();
  initClock();
  initScreen();

  initSensors();
  initPomp();

  timerSensorsCheck.setDuration(repeatIntervalSensorsCheck);
}

uint32_t test_time = 0;

void loop() {
  // сначала делаем дешевые операции, все дорогие делаем в конце функции
  if (Serial3.available() > 0) {
    String message = Serial3.readStringUntil('\n');
    message.trim();
    writeln(message);
    // todo process command
    return;
  }

  if (global_state.updated) {
    JsonDocument toSend = serializeState(global_state);
    toSend[command_key] = arduino_command_state;
    String outJson;
    serializeJson(toSend, outJson);
    Serial3.println(outJson);
    writeln(outJson);
    global_state.updated = false;
    loopScreen();  // todo придумать более красивую схему обновления экрана
    return;
  }

  for (int i = 0; i < 16; i++) {
    if (isWaterNowButtonPressed(i)) {
      startWaterPlant(i);

      while (isWaterNowButtonPressed(i)) {
      }

      stopWaterPlant(i);
      loopScreen();
      return;
    }
  }

  updatePlantsState();

  if (timerSensorsCheck.expired()) {
    timerSensorsCheck.setDuration(repeatIntervalSensorsCheck);
    loopSensors();
    return;
  }

  if (true) {
    return;
  }

  if (runNextDayTask()) { //todo проверка на корректность времени
    stateUpdated();
    writeln("Runing daily task...");
    buzzerCommand();

    // поливаем первое растение 20мл
    startWaterPlant(0);
    delay(7000);
    stopWaterPlant(0);

    // поливаем второе растение 5мл
    startWaterPlant(1);
    delay(1100);
    stopWaterPlant(1);
    loopScreen();
    return;
  }

  if (IS_DEBUG) {
    return;
  }

  // главная мысль, если нужно выполнить команду, сразу ее сделать, а потом
  // запустить алгоритм заново сначала нужно вытащить команду от Алисы и
  // выполнить ее потом проверить стейт потом поливать потом послать данные
  // Алисе потом уже синхронизация времени

  writeln("--->>> start main cycle");

  // проверить надо ли обновлять стейт, сделать обновление

  // нарисовать экран по изменению стейта

  // послать оповещения пользователям
  // послать стейт алисе

  // учесть резкие изменения стейта?
  // пройтись по стейту - нужно ли поливать, сделать поливку
  // сначала обновить стейт, полить, обновить стейт

  // слушаем кнопку принудительного полива - короткое нажание - проверка,
  // длительное - поливка алгоритмом через пик, длинная задержка -
  // принудительная проливка

  // получаем команду от алисы (с фильтрацией дублей) - отображение на экране
  // ---busy---, отослать статус "начали выполнять" и "выполнили"
}

// void waterPlant(Plant plant) {}

// bool checkAlisa() {}