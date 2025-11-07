#include <Arduino.h>
#include <Ds1302.h>
#include <EEPROM.h>
#include <time.h>

#define PIN_ENA 41
#define PIN_CLK 45
#define PIN_DAT 43

Ds1302 rtc(PIN_ENA, PIN_CLK, PIN_DAT);

// const static char* WeekDays[] = {"Monday", "Tuesday",  "Wednesday",
// "Thursday", "Friday", "Saturday", "Sunday"};

// Ds1302::DateTime nextTaskRuning;
// Ds1302::DateTime startUpDate;

Ds1302::DateTime getNow() {
  Ds1302::DateTime now;
  rtc.getDateTime(&now);
  return now;
}

void stateUpdated() {
  // todo подумать о том, чтобы сохранять источник, время, "широту обновления"
  global_state.updated = true;
}

void initClock() {
  writeln("Init clock");
  // initialize the RTC
  rtc.init();
  writeln("Clock inited");

  // установить первоначальное время
  if (false) {
    writeln("RTC is halted. Setting time...");
    // rtc.halt();
    Ds1302::DateTime dt = {
        .year = 25,
        .month = Ds1302::MONTH_OCT,  // в библиотеке опечатка, новая версия не
                                     // подхватывается
        .day = 3,
        .hour = 14,
        .minute = 34,
        .second = 00,
        .dow = Ds1302::DOW_FRI};

    rtc.setDateTime(&dt);
  }
  loadNextTaskRuning();
  if (false) {
    Ds1302::DateTime firstAction;
    firstAction.year = 25;
    firstAction.month = 10;
    firstAction.day = 3;
    firstAction.hour = 15;
    firstAction.minute = 0;
    firstAction.second = 0;
    global_state.nextTaskRuning = firstAction;
    saveNextTaskRuning();
  }

  global_state.startUpDate = getNow();
}

void saveNextTaskRuning() {
  int address = 0;
  EEPROM.put(address, global_state.nextTaskRuning);
  writeln("Saved next water task:");
  writeln(dateToString(global_state.nextTaskRuning));
}

void loadNextTaskRuning() {
  int address = 0;
  EEPROM.get(address, global_state.nextTaskRuning);
  writeln("Loaded next water task:");
  writeln(dateToString(global_state.nextTaskRuning));
}

void normalizeDate(Ds1302::DateTime newDate) {
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

bool isNowAfter(Ds1302::DateTime check) {
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

bool runNextDayTask() {
  if (isNowAfter(global_state.nextTaskRuning)) {
    Serial.print(dateToString(global_state.nextTaskRuning));
    Serial.print(" < ");
    Serial.print(dateToString(getNow()));
    Serial.println("!!!!!!!!!!!!!!!!!!!!");

    global_state.nextTaskRuning = getNow();
    global_state.nextTaskRuning.day++;
    normalizeDate(global_state.nextTaskRuning);
    saveNextTaskRuning();

    return true;
  }
  return false;
}