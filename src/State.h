#include <Ds1302.h>

#define PLANT_IS_ON 10
#define PLANT_IS_OFF_USER 0

struct Plant {
  // краткое описание растения (горшок, название и тд)
  String plantName;
  // включено ли растение 0 - выключен тумблер 1 - отключение по ошибке
  // PLANT_IS_ON - включено
  int isOn;
  // сколько в процентах влажности 0..99
  int parrots;
  // частота полива в часах
  // баунд принудительной поливки
};

struct State {
  Plant plants[16];

  // следующая глобальная проливка растений
  Ds1302::DateTime nextTaskRuning;
  // загрузка приложения
  Ds1302::DateTime startUpDate;

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
