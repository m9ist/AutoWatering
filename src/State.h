#include <ArduinoJson.h>
#include <Ds1302.h>

#define PLANT_IS_ON 10
#define PLANT_IS_OFF_USER 0
#define PLANTS_AMOUNT 16

const char* command_key = "command";

const char* esp_command_log = "esp_log";
const char* esp_command_start_work = "esp_start_working";
const char* esp_command_connect_wifi = "esp_wait_wifi";
const char* esp_command_inited = "esp_inited";

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
  out["temperature"] = state.temperature;
  out["humidity"] = state.humidity;

  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    out["plants"][i]["isOn"] = state.plants[i].isOn;
    out["plants"][i]["plantName"] = state.plants[i].plantName;
    out["plants"][i]["parrots"] = state.plants[i].parrots;
    out["plants"][i]["originalValue"] = state.plants[i].originalValue;
  }
  
  return out;
}

State deserializeState(JsonDocument doce) {
  JsonDocument doc;
  State out;
  out.temperature = doc["temperature"];
  out.humidity = doc["humidity"];

  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    out.plants[i].isOn = doc["plants"][i]["isOn"];
    const char* plantName = doc["plants"][i]["plantName"];
    out.plants[i].plantName = plantName;
    out.plants[i].parrots = doc["plants"][i]["parrots"];
    out.plants[i].originalValue = doc["plants"][i]["originalValue"];
  }

  return out;
}