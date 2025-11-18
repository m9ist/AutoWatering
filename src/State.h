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

#define COMMAND_KEY F("command")
#define ESP_COMMAND_LOG F("esp_log")
#define ESP_COMMAND_TIME_SYNCED F("esp_ntp_synchronized")
#define ESP_COMMAND_WATER_PLANT F("esp_water_plant")
#define ESP_COMMAND_CONFIG_PLANT F("esp_plant_config")
#define ESP_COMMAND_DAILY_TASK F("esp_daily_task")

#define ARDUINO_COMMAND_STATE F("arduino_state_update")
#define ARDUINO_SEND_TELEGRAM F("arduino_telegram")

struct Plant {
  // включено ли растение PLANT_IS_OFF_USER - выключен тумблер ??? - отключение
  // по ошибке PLANT_IS_ON - включено
  int isOn = PLANT_IS_UNDEFINED;
  // краткое описание растения (горшок, название и тд)
  String plantName = "";
  // сколько в процентах влажности 0..99
  int parrots = 0;
  // оригинальная влажность от датчика влажности
  int originalValue = UNDEFINED_PLANT_VALUE;

  int dailyAmountMl = 0;
};

struct State {
  // если что-то где-то обновилось и можно отослать стейт в esp и лог
  bool updated = false;

  bool sdInited = false;
  bool pompIsOn = false;
  bool espConnectedAndTimeSynced = false;

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
  int checkFrequencyInMinutes = 30;
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
  return plant.isOn == PLANT_IS_ON ||
         plant.originalValue < UNDEFINED_PLANT_VALUE || plant.plantName != "";
}

JsonDocument serializeState(State state) {
  JsonDocument out;
  out[COMMAND_KEY] = ARDUINO_COMMAND_STATE;
  out[F("t")] = state.temperature;
  out[F("h")] = state.humidity;

  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    if (!isDefined(state.plants[i])) continue;
    out[F("p")][i][F("id")] = i;
    out[F("p")][i][F("on")] = state.plants[i].isOn;
    out[F("p")][i][F("d")] = state.plants[i].plantName;
    out[F("p")][i][F("p")] = state.plants[i].parrots;
    out[F("p")][i][F("or")] = state.plants[i].originalValue;
  }

  return out;
}

State deserializeState(JsonDocument doc) {
  State out;
  out.temperature = doc[F("t")];
  out.humidity = doc[F("h")];

  for (size_t i = 0; i < doc[F("p")].size(); i++) {
    int id = doc[F("p")][i][F("id")];
    out.plants[id].isOn = doc[F("p")][i][F("on")];
    const char* plantName = doc[F("p")][i][F("d")];
    out.plants[id].plantName = plantName;
    out.plants[id].parrots = doc[F("p")][i][F("p")];
    out.plants[id].originalValue = doc[F("p")][i][F("or")];
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

void saveStateEEPROM() {
  // todo<<<<<<<<< заменить функции в clock
}
