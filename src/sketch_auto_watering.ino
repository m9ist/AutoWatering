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
  updatePlantsState(false);
  initPomp();

  timerSensorsCheck.setDuration(Timer::Seconds(5));
  writeln(F("End init arduino"));
}

uint32_t test_time = 0;

void sendTelegram(String message) {
  JsonDocument toSend;
  toSend[COMMAND_KEY] = ARDUINO_SEND_TELEGRAM;
  toSend[F("message")] = (String)F("Arduino: ") + message;
  String out;
  serializeJson(toSend, out);
  writeln(out);
  comm.communicationSendMessage(out);
}

void processEspCommand(JsonDocument& doc) {
  const char* command = doc[COMMAND_KEY];
  if ((String)ESP_COMMAND_LOG == command) {
    // уже залогировали, ничего не делаем
    return;
  }

  if ((String)ESP_COMMAND_TIME_SYNCED == command) {
    tm timeinfo = deserializeTimeInfo(doc);
    setupDate(timeinfo);
    global_state.espConnectedAndTimeSynced = true;
    loopScreen();
    return;
  }

  if ((String)ESP_COMMAND_WATER_PLANT == command) {
    int id = doc[F("plantId")];
    int amount = doc[F("amountMl")];
    // waterPlant(id, amount); //<<<<<<<<<<<<<
    String info = (String)F("Did nothing WATERING with ") + id + " " + amount;
    sendTelegram(info);
    return;
  }

  if ((String)ESP_COMMAND_CONFIG_PLANT == command) {
    int id = doc[F("plantId")];
    int amount = doc[F("amountMl")];
    global_state.plants[id].dailyAmountMl = amount;
    saveStateEEPROM();
    String info = "Current config: "; // оборачивать в F нельзя, виснет...
    // info += F("Current config: ");
    for (int i = 0; i < PLANTS_AMOUNT; i++) {
      if (global_state.plants[i].dailyAmountMl > 0) {
        info += (String)F("plant ") + i + F(" = ") +
                global_state.plants[i].dailyAmountMl + F(" ml, ");
      }
    }
    sendTelegram(info);
    return;
  }

  if ((String)ESP_COMMAND_DAILY_TASK == command) {
    sendTelegram(F("Do nothing with daily task command"));
    return;
  }

  writeln((String)F("Unknown command ") + command);
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
      processEspCommand(doc);
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

  /*
  for (int i = 0; i < 16; i++) {
    if (isWaterNowButtonPressed(i)) {
      // drawScreenMessage((String)F("Start water plant ") + i);//todo
  <<<<<<<<<<<<<<<<<<<< победить фигню startWaterPlant(i);

      while (isWaterNowButtonPressed(i)) {
      }

      String info = stopWaterPlant(i);
      // drawScreenMessage(info); //todo <<<<<<<<<<<<<<<<<<<< победить фигню
      // todo <<<<<< подумать как отказаться от этого, обдумать всю схему работы
      // с экраном
      delay(3000);

      loopScreen();
      return;
    }
  }
  //*/

  updatePlantsState(true);

  if (timerSensorsCheck.expired()) {
    timerSensorsCheck.setDuration(repeatIntervalSensorsCheck);
    loopSensors();
    return;
  }

  if (true) {
    return;
  }

  /*
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
  //*/
}
