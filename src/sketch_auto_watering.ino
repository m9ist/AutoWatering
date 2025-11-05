#include <Ds1302.h>
#include <State.h>

#define IS_DEBUG true

// Alt + Shift + F - автоформатирование кода
// Ctrl + Alt + B - компиляция
// Ctrl + Alt + U - загрузка прошивки

State global_state;

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);
  writeln("Hellow world!");
  if (IS_DEBUG) {
    return;
  }
  initLogging();
  initClock();
  initScreen();

  initSensors();
  initPomp();
}

uint32_t test_time = 0;

void loop() {
  if (IS_DEBUG) {
    if (Serial3.available() > 0) {
      String command = Serial3.readStringUntil('\n');
      writeln(command);
      // todo process command
      return;
    }

    if (test_time < millis()) {
      test_time = millis() + 1000;
      Serial3.println("ping from arduino");
      return;
    }

    return;
  }
  

  for (int i = 0; i < 16; i++) {
    if (isWaterNowButtonPressed(i)) {
      startWaterPlant(i);

      while (isWaterNowButtonPressed(i)) {
      }

      stopWaterPlant(i);
      return;
    }
  }

  if (runNextDayTask()) {
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
    return;
  }

  updatePlantsState();
  loopSensors();
  delay(500);  // почему-то без этой задержки приложение падает...
  loopScreen();
  delay(3000);
  // loopPomp();
  // loopClock();
  // loopScreen();
  // loopMultuplexer();

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