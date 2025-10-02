#include <TimeLib.h>  // Timelib in library manager
// #include <WString.h>

#define PLANT_IS_ON 10

struct Plant {
  // краткое описание растения (горшок, название и тд)
  String plantName;
  // включено ли растение 0 - выключен тумблер 1 - отключение по ошибке PLANT_IS_ON - включено
  int isOn;
  // сколько в процентах влажности
  int parrots;
  // частота полива в часах
  // баунд принудительной поливки
};

struct State {
  Plant plants[16];

  // последняя проверка датчиков влажности
  time_t lastCheck;
  // частота проверки датчиков влажности в минутах
  int checkFrequencyInMinutes = 30;
  // частота отправки данных в яндекс
  int sendIotFrequencyInMinutes = 60;
  // Время последней синхронизации часов
  // Список критических ошибок
};
