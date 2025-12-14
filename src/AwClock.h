#include <Arduino.h>
#include <AwLogging.h>
#include <Ds1302.h>
#include <State.h>
#include <time.h>

#define PIN_ENA 41
#define PIN_CLK 45
#define PIN_DAT 43

class AwClock {
 private:
  Ds1302 rtc = Ds1302(PIN_ENA, PIN_CLK, PIN_DAT);

  // const static char* WeekDays[] = {"Monday", "Tuesday",  "Wednesday",
  // "Thursday", "Friday", "Saturday", "Sunday"};

  void normalizeDate(Ds1302::DateTime newDate);

  bool isNowAfter(Ds1302::DateTime check);

 public:
  AwClock() {};
  ~AwClock() {};

  Ds1302::DateTime getNow() {
    Ds1302::DateTime now;
    rtc.getDateTime(&now);
    return now;
  }

  bool runNextDayTask(State& state, AwLogging& logger) {
    if (isNowAfter(state.nextTaskRuning)) {
      logger.writeln(dateToString(state.nextTaskRuning) + F(" < ") +
                     dateToString(getNow()) + F("!!!!!!!!!!!!!!!!!!!!"));

      state.nextTaskRuning = getNow();
      state.nextTaskRuning.day++;
      normalizeDate(state.nextTaskRuning);

      return true;
    }
    return false;
  }

  void initClock(State& state, AwLogging& logger) {
    logger.writeln(F("Init clock"));
    // initialize the RTC
    rtc.init();
    logger.writeln(F("Clock inited"));

    state.startUpDate = getNow();
  }

  void setupDate(tm timeinfo, AwLogging& logger) {
    logger.writeln(F("Setting time..."));
    Ds1302::DateTime dt = {.year = timeinfo.tm_year - 100,
                           .month = 1 + timeinfo.tm_mon,
                           .day = timeinfo.tm_mday,
                           .hour = timeinfo.tm_hour,
                           .minute = timeinfo.tm_min,
                           .second = timeinfo.tm_sec};

    rtc.setDateTime(&dt);
  }
};

void AwClock::normalizeDate(Ds1302::DateTime newDate) {
  uint8_t daysInMonth = month_length(newDate.year, newDate.month - 1);

  if (newDate.second >= 60) {
    newDate.second = newDate.second - 60;
    newDate.minute++;
  }

  if (newDate.minute >= 60) {
    newDate.minute = newDate.minute - 60;
    newDate.hour++;
  }

  if (newDate.hour >= 24) {
    newDate.hour = newDate.hour - 24;
    newDate.day++;
  }

  if (newDate.day > daysInMonth) {
    newDate.day = newDate.day - daysInMonth;
    newDate.month++;
  }

  if (newDate.month > 12) {
    newDate.month = newDate.month - 12;
    newDate.year++;
  }
}

bool AwClock::isNowAfter(Ds1302::DateTime check) {
  Ds1302::DateTime now = getNow();

  if (check.year < now.year) return true;
  if (check.year > now.year) return false;

  if (check.month < now.month) return true;
  if (check.month > now.month) return false;

  if (check.day < now.day) return true;
  if (check.day > now.day) return false;

  if (check.hour < now.hour) return true;
  if (check.hour > now.hour) return false;

  if (check.minute < now.minute) return true;
  if (check.minute > now.minute) return false;

  return check.second < now.second;
}
