#include <ArduinoJson.h>
#include <Ds1302.h>
#include <Time.h>

#define PLANT_IS_ON 10
#define PLANT_IS_OFF_USER 0
#define PLANTS_AMOUNT 16
#define DATA_CHUNK_SIZE 62  // SERIAL_TX_BUFFER_SIZE

const char* command_key = "command";

const char* esp_command_log = "esp_log";
const char* esp_command_start_work = "esp_start_working";
const char* esp_command_connect_wifi = "esp_wait_wifi";
const char* esp_command_inited = "esp_inited";
const char* esp_command_time_synced = "esp_ntp_synchronized";

const char* arduino_command_state = "arduino_state_update";

struct Plant {
  // включено ли растение PLANT_IS_OFF_USER - выключен тумблер ??? - отключение
  // по ошибке PLANT_IS_ON - включено
  int isOn;
  // краткое описание растения (горшок, название и тд)
  String plantName;
  // сколько в процентах влажности 0..99
  int parrots;
  // оригинальная влажность от датчика влажности
  int originalValue;
  // частота полива в часах
  // баунд принудительной поливки
};

struct State {
  // если что-то где-то обновилось и можно отослать стейт в esp и лог
  bool updated = false;

  bool sdInited = false;

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

  bool pompIsOn = false;
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

JsonDocument serializeState(State state) {
  JsonDocument out;
  out[command_key] = arduino_command_state;
  out[F("t")] = state.temperature;
  out[F("h")] = state.humidity;

  for (int i = 0; i < PLANTS_AMOUNT; i++) {
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

  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    out.plants[i].isOn = doc[F("p")][i][F("on")];
    const char* plantName = doc[F("p")][i][F("d")];
    out.plants[i].plantName = plantName;
    out.plants[i].parrots = doc[F("p")][i][F("p")];
    out.plants[i].originalValue = doc[F("p")][i][F("or")];
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
