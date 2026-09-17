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
// Помпы: позиционные номера, наружу (Telegram, aw/state) — pump1/pump2.
// Индекс в массивах стейта, не пин: пины живут в Pomp.h.
#define PUMPS_AMOUNT 2
#define PUMP_1 0
#define PUMP_2 1
// todo удалить: не используется, дубль COMMUNICATION_DATA_CHUNK_SIZE
#define DATA_CHUNK_SIZE 62  // SERIAL_TX_BUFFER_SIZE
#define UNDEFINED_PLANT_VALUE 1022
// максимальный объём одной команды полива (и дневной нормы), защита от опечаток
#define MAX_WATER_AMOUNT_ML 200

#define COMMAND_KEY F("c")
#define ESP_COMMAND_LOG F("esp_log")
#define ESP_COMMAND_TIME_SYNCED F("esp_ntp")
#define ESP_COMMAND_WATER_PLANT F("esp_water")
#define ESP_COMMAND_CONFIG_PLANT F("esp_plant_conf")
#define ESP_COMMAND_DAILY_TASK F("esp_daily")
#define ESP_COMMAND_CHECK_VALVES F("esp_check_valves")
#define ESP_COMMAND_SWITCH_PUMP F("esp_pump")

#define ARDUINO_COMMAND_STATE F("state")
#define ARDUINO_SEND_TELEGRAM F("arduino_tg")

#define EEPROM_VERSION 7

// Изменил, обнови EEPROM_VERSION
struct Plant {
  // включено ли растение PLANT_IS_OFF_USER - выключен тумблер ??? - отключение
  // по ошибке PLANT_IS_ON - включено
  uint8_t isOn = PLANT_IS_UNDEFINED;
  // краткое описание растения (горшок, название и тд)
  char plantName[10] = "";
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

  // Активная помпа: PUMP_1 (D6) или PUMP_2 (D5). Переживает ребут, иначе
  // после каждого ресета система возвращалась бы на помпу, которую только
  // что признали дохлой. Дефолт PUMP_2 — поведение прошивки до issue #22.
  uint8_t activePump = PUMP_2;
  // Результат последнего пуска каждой помпы: сколько мл намерил расходомер
  // и когда это было (dateToEpoch, 0 — ни разу не запускалась). Вердикт
  // «резервная жива» ручной, по этим двум числам в /state (issue #22).
  uint16_t lastPumpMl[PUMPS_AMOUNT] = {0, 0};
  uint32_t lastPumpRunAt[PUMPS_AMOUNT] = {0, 0};
  // Снимок часов Mega на момент сборки Стейта: по нему aw-server считает
  // возраст пусков помп, не пытаясь свести свои часы с RTC ардуины.
  uint32_t nowEpoch = 0;
  bool espConnectedAndTimeSynced = false;
  bool temperatureSensorInited = false;
  // bool hasWaterLevel = false;

  int freeMemorySize = 0;

  Plant plants[PLANTS_AMOUNT];

  // следующая глобальная проливка растений
  // todo не используется: планировщик не реализован (см. AwClock::runNextDayTask)
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
  // todo удалить: не используется, на ESP свой таймер
  int sendIotFrequencyInMinutes = 60;
  // Время последней синхронизации часов
  // Список критических ошибок
};

// функции в хедере, включаемом из нескольких мест — обязаны быть inline (ODR)
inline String dateToString(Ds1302::DateTime now) {
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

// Время RTC в линейные секунды (эпоха avr-libc — с 2000-01-01) для
// арифметики возраста: сравнивать Ds1302::DateTime поэлементно ради
// «сколько минут назад» неудобно. 0 считаем за «события не было» —
// коллизия только с полуночью 2000-01-01, то есть с неинициализированным
// RTC, где значение и так бессмысленно.
// Считаем сами, а не через libc: mk_gmtime есть только в avr-libc (ESP не
// собирается), а mktime/timegm на ESP отсчитывают от 1970 против 2000 у
// AVR — один и тот же кадр получал бы разные числа на разных прошивках.
inline uint32_t dateToEpoch(Ds1302::DateTime dt) {
  // дней с начала года до начала месяца, невисокосный год
  static const uint16_t daysBeforeMonth[12] = {0,   31,  59,  90,  120, 151,
                                               181, 212, 243, 273, 304, 334};
  if (dt.month < 1 || dt.month > 12) return 0;
  uint16_t year = dt.year;  // 00-99 от 2000
  // 2000 — високосный, и в диапазоне 2000-2099 правило «каждые 4 года»
  // работает без исключений (2100 уже вне разрядности Ds1302)
  uint32_t days = (uint32_t)year * 365 + (year + 3) / 4;
  days += daysBeforeMonth[dt.month - 1];
  if (dt.month > 2 && (year % 4) == 0) days++;
  days += dt.day - 1;
  return days * 86400UL + (uint32_t)dt.hour * 3600UL +
         (uint32_t)dt.minute * 60UL + dt.second;
}

inline bool isDefined(const Plant& plant) {
  // todo <<<<<< когда будут ошибки учесть их
  return plant.isOn == PLANT_IS_ON;
  //|| plant.originalValue < UNDEFINED_PLANT_VALUE || plant.plantName != "";
}

inline JsonDocument serializeState(const State& state) {
  JsonDocument out;
  out[COMMAND_KEY] = ARDUINO_COMMAND_STATE;
  int hum = state.humidity * 10;
  int temp = state.temperature * 10;
  out[F("t")] = temp;
  out[F("h")] = hum;
  out[F("ram")] = state.freeMemorySize;

  // Помпы (issue #22): активная + последний результат каждой. Время — в
  // часах Mega: возраст aw-server считает как now - pat[i], в одной шкале,
  // не сводя свои часы с RTC ардуины.
  out[F("pmp")] = state.activePump;
  out[F("now")] = state.nowEpoch;
  for (int i = 0; i < PUMPS_AMOUNT; i++) {
    out[F("pml")][i] = state.lastPumpMl[i];
    out[F("pat")][i] = state.lastPumpRunAt[i];
  }

  int id = 0;
  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    // акууратенее с id и i
    if (!isDefined(state.plants[i])) continue;
    out[F("p")][id][F("id")] = i;
    out[F("p")][id][F("on")] = state.plants[i].isOn;
    // out[F("p")][id][F("d")] = state.plants[i].plantName;
    // out[F("p")][id][F("d")] = state.plants[i].plantName;
    out[F("p")][id][F("or")] = state.plants[i].originalValue;
    out[F("p")][id][F("m")] = state.plants[i].dailyAmountMl;
    id++;
  }

  return out;
}

inline State deserializeState(const JsonDocument& doc) {
  State out;
  int temp = doc[F("t")];
  int hum = doc[F("h")];
  out.temperature = (float)temp / 10;
  out.humidity = (float)hum / 10;
  out.freeMemorySize = doc[F("ram")];

  out.activePump = doc[F("pmp")] | PUMP_2;
  out.nowEpoch = doc[F("now")] | 0UL;
  for (int i = 0; i < PUMPS_AMOUNT; i++) {
    out.lastPumpMl[i] = doc[F("pml")][i] | 0;
    out.lastPumpRunAt[i] = doc[F("pat")][i] | 0UL;
  }

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
    out.plants[id].originalValue = doc[F("p")][i][F("or")];
    out.plants[id].dailyAmountMl = doc[F("p")][i][F("m")];
  }

  return out;
}

inline void serializeTimeInfo(tm timeinfo, JsonDocument& out) {
  out[F("tm_sec")] = timeinfo.tm_sec;
  out[F("tm_min")] = timeinfo.tm_min;
  out[F("tm_hour")] = timeinfo.tm_hour;
  out[F("tm_mday")] = timeinfo.tm_mday;
  out[F("tm_wday")] = timeinfo.tm_wday;
  out[F("tm_mon")] = timeinfo.tm_mon;
  out[F("tm_year")] = timeinfo.tm_year;
}

inline tm deserializeTimeInfo(const JsonDocument& doc) {
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