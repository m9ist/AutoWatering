#include <ArduinoJson.h>
#include <Communication.h>
#include <Ds1302.h>
#include <State.h>
#include <Time.h>
#include <Timer.h>

#define IS_DEBUG true
#define DATA_CHUNK_SIZE 63  // SERIAL_TX_BUFFER_SIZE

// Alt + Shift + F - автоформатирование кода
// Ctrl + Alt + B - компиляция
// Ctrl + Alt + U - загрузка прошивки

State global_state;

void stateUpdated() {
  // todo подумать о том, чтобы сохранять источник, время, "широту обновления"
  global_state.updated = true;
}

Timer timerSensorsCheck;
const Duration repeatIntervalSensorsCheck = Timer::Minutes(1);
Communication comm = Communication(Serial3, Serial, false);

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

void logFreeRam() { writeln((String)F("Free RAM: ") + freeRam()); }

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);
  while (!Serial || !Serial3) {
  }

  writeln(F("Start working"));
  logFreeRam();
  initLogging();
  initClock();
  initScreen();

  initSensors();
  initPomp();

  timerSensorsCheck.setDuration(Timer::Seconds(5));
}

uint32_t test_time = 0;

void sendSerial(String message) {
  // if (message.length() == 0) {
  //   writeln(F("Got empty message to send. Exit."));
  //   return;
  // }
  // todo inline
  // sendSerial(message, Serial3);
  
}

void loop() {
  // сначала делаем дешевые операции, все дорогие делаем в конце функции
  comm.communicationTick();
  // if (Serial3.available() > 0) {
  // String message = Serial3.readStringUntil('\n');
  // message.trim();
  if (comm.communicationHasMessage()) {
    String message = comm.communicationGetMessage();
    writeln(message);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error != DeserializationError::Ok) {
      writeln((String)F("Can't deserialize ") + error.c_str());
    } else {
      const char* command = doc[command_key];
      if (strcmp(command, esp_command_log) == 0) {
        // уже залогировали, ничего не делаем
      } else if (strcmp(command, esp_command_connect_wifi) == 0) {
        // todo
      } else if (strcmp(command, esp_command_start_work) == 0) {
        // todo
      } else if (strcmp(command, esp_command_inited) == 0) {
        // todo
      } else if (strcmp(command, esp_command_time_synced) == 0) {
        tm timeinfo = deserializeTimeInfo(doc);
        setupDate(timeinfo);
      } else {
        writeln((String)F("Unknown command ") + command);
      }
    }
    // todo process command
    return;
  }

  if (global_state.updated) {
    logFreeRam();
    JsonDocument toSend = serializeState(global_state);
    String out;
    writeln((String)F("Expected string length ") + (measureJson(toSend) + 1));
    serializeJson(toSend, out);
    writeln(out);
    comm.communicationSendMessage(out);
    global_state.updated = false;
    loopScreen();  // todo придумать более красивую схему обновления экрана
    logFreeRam();
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

  if (runNextDayTask()) {  // todo проверка на корректность времени
    writeln(F("Runing daily task..."));
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

  writeln(F("--->>> start main cycle"));

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