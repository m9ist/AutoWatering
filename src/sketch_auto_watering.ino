#include <ArduinoJson.h>
#include <AwLogging.h>
#include <Communication.h>
#include <Ds1302.h>
#include <EEPROM.h>
#include <Screen.h>
#include <State.h>
#include <Time.h>
#include <Timer.h>

#define IS_DEBUG true

// Alt + Shift + F - автоформатирование кода
// Ctrl + Alt + B - компиляция
// Ctrl + Alt + U - загрузка прошивки

State global_state;
AwLogging logger;

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

void logFreeRam() { logger.writeln((String)F("Free RAM: ") + freeRam()); }

void loadStateEEPROM() {
  int address = 0;
  int version;
  EEPROM.get(address, version);
  if (version != EEPROM_VERSION) {
    Serial.println("Bad version of EEPROM in memory");
  } else {
    address += sizeof(int);
    EEPROM.get(address, global_state);
    Serial.println("Loaded state from eeprom");
  }
}

void saveStateEEPROM() {
  int address = 0;
  int version = EEPROM_VERSION;
  EEPROM.put(address, version);
  address += sizeof(int);
  EEPROM.put(address, global_state);
  logger.writeln("Saved state to eeprom");
}

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);
  while (!Serial || !Serial3) {
  }

  Serial.println(F("Start working"));
  logFreeRam();
  loadStateEEPROM();
  global_state.sdInited = false;
  global_state.espConnectedAndTimeSynced = false;
  logger.init(global_state);
  initClock();
  initScreen(logger);

  initSensors();
  updatePlantsState(false);
  initPomp();

  timerSensorsCheck.setDuration(Timer::Seconds(5));
  logger.writeln(F("End init arduino"));
}

uint32_t test_time = 0;

void sendTelegram(String message) {
  JsonDocument toSend;
  toSend[COMMAND_KEY] = ARDUINO_SEND_TELEGRAM;
  toSend[F("message")] = (String)F("Arduino: ") + message;
  String out;
  serializeJson(toSend, out);
  logger.writeln(out);
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
    loopScreen(global_state);
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
    String info = "Current config: ";  // оборачивать в F нельзя, виснет...
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

  logger.writeln((String)F("Unknown command ") + command);
}

void loop() {
  // сначала делаем дешевые операции, все дорогие делаем в конце функции
  comm.communicationTick();

  if (comm.communicationHasMessage()) {
    String message = comm.communicationGetMessage();
    logger.writeln(message);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error != DeserializationError::Ok) {
      logger.writeln((String)F("Can't deserialize ") + error.c_str());
    } else {
      processEspCommand(doc);
    }
    return;
  }

  if (global_state.updated) {
    logFreeRam();
    JsonDocument toSend = serializeState(global_state);
    String out;
    logger.writeln((String)F("Expected string length ") + (measureJson(toSend) + 1));
    serializeJson(toSend, out);
    logger.writeln(out);
    comm.communicationSendMessage(out);
    global_state.updated = false;
    // todo придумать более красивую схему обновления экрана
    loopScreen(global_state);  
    logFreeRam();
    return;
  }

  //*
  for (int i = 0; i < 16; i++) {
    if (isWaterNowButtonPressed(i)) {
      // drawScreenMessage((String)F("Start water plant ") + i);  // todo
      // drawScreenMessage(F("Test"), logger.writeln);
      // <<<<<<<<<<<<<<<<<<<< победить фигню startWaterPlant(i);

      while (isWaterNowButtonPressed(i)) {
      }

      // String info = stopWaterPlant(i);
      // drawScreenMessage(info); //todo <<<<<<<<<<<<<<<<<<<< победить фигню
      // todo <<<<<< подумать как отказаться от этого, обдумать всю схему работы
      // с экраном
      delay(3000);

      loopScreen(global_state);
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
    logger.writeln(F("Runing daily task..."));
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