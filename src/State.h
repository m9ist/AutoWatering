#ifndef STATE_H
#define STATE_H
#include <ArduinoJson.h>
#include <Ds1302.h>
#include <Time.h>

#define PLANT_IS_ON 10
#define PLANT_IS_OFF_USER 1
#define PLANT_IS_OFF_EXCEPTION 2
#define PLANT_IS_UNDEFINED -1
#define PLANTS_AMOUNT 16
#define DATA_CHUNK_SIZE 62  // SERIAL_TX_BUFFER_SIZE
#define UNDEFINED_PLANT_VALUE 1022

#define COMMAND_KEY F("c")
#define ESP_COMMAND_LOG F("esp_log")
#define ESP_COMMAND_TIME_SYNCED F("esp_ntp")
#define ESP_COMMAND_WATER_PLANT F("esp_water")
#define ESP_COMMAND_CONFIG_PLANT F("esp_plant_conf")
#define ESP_COMMAND_DAILY_TASK F("esp_daily")

#define ARDUINO_COMMAND_STATE F("state")
#define ARDUINO_SEND_TELEGRAM F("arduino_tg")

#define EEPROM_VERSION 4

// Изменил, обнови EEPROM_VERSION
struct Plant {
  // включено ли растение PLANT_IS_OFF_USER - выключен тумблер ??? - отключение
  // по ошибке PLANT_IS_ON - включено
  uint8_t isOn = PLANT_IS_UNDEFINED;
  // краткое описание растения (горшок, название и тд)
  char plantName[10] = "";
  // сколько в процентах влажности 0..99
  uint8_t parrots = 0;
  // оригинальная влажность от датчика влажности
  uint16_t originalValue = UNDEFINED_PLANT_VALUE;

  uint16_t dailyAmountMl = 0;
};

// Изменил, обнови EEPROM_VERSION
struct State {
  // если что-то где-то обновилось и можно отослать стейт в esp и лог
  bool updated = false;

  bool sdInited = false;
  bool pompIsOn = false;
  bool espConnectedAndTimeSynced = false;
  bool temperatureSensorInited = false;
  // bool hasWaterLevel = false;

  int freeMemorySize = 0;

  Plant plants[PLANTS_AMOUNT];

  // следующая глобальная проливка растений
  Ds1302::DateTime nextTaskRuning;
  // загрузка приложения
  Ds1302::DateTime startUpDate;

  Ds1302::DateTime lastSensorCheck;

  float temperature;
  float humidity;

  // последняя проверка датчиков влажности
  // time_t lastCheck;
  // частота проверки датчиков влажности в минутах
  // int checkFrequencyInMinutes = 30;
  // int checkFrequencyInMinutes = 30;
  // частота отправки данных в яндекс
  int sendIotFrequencyInMinutes = 60;
  // Время последней синхронизации часов
  // Список критических ошибок
};

String dateToString(Ds1302::DateTime now) {
  String out;

  out += "20";
  out += now.year;  // 00-99
  out += "-";
  if (now.month < 10) out += '0';
  out += now.month;  // 01-12
  out += '-';
  if (now.day < 10) out += '0';
  out += now.day;
  out += ' ';
  if (now.hour < 10) out += '0';
  out += now.hour;  // 00-23
  out += ':';
  if (now.minute < 10) out += '0';
  out += now.minute;  // 00-59
  out += ':';
  if (now.second < 10) out += '0';
  out += now.second;  // 00-59
  return out;
}

bool isDefined(Plant plant) {
  // todo <<<<<< когда будут ошибки учесть их
  return plant.isOn == PLANT_IS_ON;
  //|| plant.originalValue < UNDEFINED_PLANT_VALUE || plant.plantName != "";
  return plant.isOn == PLANT_IS_ON;
  //|| plant.originalValue < UNDEFINED_PLANT_VALUE || plant.plantName != "";
}

JsonDocument serializeState(State state) {
  JsonDocument out;
  out[COMMAND_KEY] = ARDUINO_COMMAND_STATE;
  int hum = state.humidity * 10;
  int temp = state.temperature * 10;
  out[F("t")] = temp;
  out[F("h")] = hum;
  out[F("ram")] = state.freeMemorySize;

  int id = 0;
  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    // акууратенее с id и i
    if (!isDefined(state.plants[i])) continue;
    out[F("p")][id][F("id")] = i;
    out[F("p")][id][F("on")] = state.plants[i].isOn;
    // out[F("p")][id][F("d")] = state.plants[i].plantName;
    // out[F("p")][id][F("d")] = state.plants[i].plantName;
    out[F("p")][id][F("p")] = state.plants[i].parrots;
    out[F("p")][id][F("or")] = state.plants[i].originalValue;
    out[F("p")][id][F("m")] = state.plants[i].dailyAmountMl;
    id++;
  }

  return out;
}

State deserializeState(JsonDocument doc) {
  State out;
  int temp = doc[F("t")];
  int hum = doc[F("h")];
  out.temperature = (float)temp / 10;
  out.humidity = (float)hum / 10;
  out.freeMemorySize = doc[F("ram")];

  for (size_t i = 0; i < doc[F("p")].size(); i++) {
    // акууратенее с id и i
    int id = doc[F("p")][i][F("id")];
    out.plants[id].isOn = doc[F("p")][i][F("on")];
    // const char* plantName = doc[F("p")][i][F("d")];
    // strlcpy(out.plants[id].plantName, plantName,
    //         sizeof(out.plants[id].plantName));
    // const char* plantName = doc[F("p")][i][F("d")];
    // strlcpy(out.plants[id].plantName, plantName,
    //         sizeof(out.plants[id].plantName));
    out.plants[id].parrots = doc[F("p")][i][F("p")];
    out.plants[id].originalValue = doc[F("p")][i][F("or")];
    out.plants[id].dailyAmountMl = doc[F("p")][i][F("m")];
  }

  return out;
}

void serializeTimeInfo(tm timeinfo, JsonDocument& out) {
  out[F("tm_sec")] = timeinfo.tm_sec;
  out[F("tm_min")] = timeinfo.tm_min;
  out[F("tm_hour")] = timeinfo.tm_hour;
  out[F("tm_mday")] = timeinfo.tm_mday;
  out[F("tm_wday")] = timeinfo.tm_wday;
  out[F("tm_mon")] = timeinfo.tm_mon;
  out[F("tm_year")] = timeinfo.tm_year;
}

tm deserializeTimeInfo(JsonDocument doc) {
  tm timeinfo;
  timeinfo.tm_sec = doc[F("tm_sec")];
  timeinfo.tm_min = doc[F("tm_min")];
  timeinfo.tm_hour = doc[F("tm_hour")];
  timeinfo.tm_mday = doc[F("tm_mday")];
  timeinfo.tm_wday = doc[F("tm_wday")];
  timeinfo.tm_mon = doc[F("tm_mon")];
  timeinfo.tm_year = doc[F("tm_year")];
  return timeinfo;
}

#endif