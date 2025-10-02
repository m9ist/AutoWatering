#include <Arduino.h>
#include <Ds1302.h>

#define PIN_ENA 41
#define PIN_CLK 45
#define PIN_DAT 43

Ds1302 rtc(PIN_ENA, PIN_CLK, PIN_DAT);

const static char* WeekDays[] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                 "Friday", "Saturday", "Sunday"};

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
        .month = Ds1302::MONTH_SET,  // в библиотеке опечатка, новая версия не
                                     // подхватывается
        .day = 25,
        .hour = 10,
        .minute = 45,
        .second = 00,
        .dow = Ds1302::DOW_THU};

    rtc.setDateTime(&dt);
  }
}

void loopClock() {
  if (rtc.isHalted()) {
    Serial.println("RTC is halted.");
  }
  // get the current time
  Ds1302::DateTime now;
  rtc.getDateTime(&now);

  Serial.print("20");
  Serial.print(now.year);  // 00-99
  Serial.print('-');
  if (now.month < 10) Serial.print('0');
  Serial.print(now.month);  // 01-12
  Serial.print('-');
  if (now.day < 10) Serial.print('0');
  Serial.print(now.day);  // 01-31
  Serial.print(' ');
  Serial.print(WeekDays[now.dow - 1]);  // 1-7
  Serial.print(' ');
  if (now.hour < 10) Serial.print('0');
  Serial.print(now.hour);  // 00-23
  Serial.print(':');
  if (now.minute < 10) Serial.print('0');
  Serial.print(now.minute);  // 00-59
  Serial.print(':');
  if (now.second < 10) Serial.print('0');
  Serial.print(now.second);  // 00-59
  Serial.println();

  delay(1000);
}

String getDateAndTime() {
  String date;
  Ds1302::DateTime now;
  rtc.getDateTime(&now);

  date += "20";
  date += now.year;  // 00-99
  date += "-";
  if (now.month < 10) date += '0';
  date += now.month;  // 01-12
  date += '-';
  if (now.day < 10) date += '0';
  date += now.day;
  date += ';';
  if (now.hour < 10) date += '0';
  date += now.hour;  // 00-23
  date += ':';
  if (now.minute < 10) date += '0';
  date += now.minute;  // 00-59
  date += ':';
  if (now.second < 10) date += '0';
  date += now.second;  // 00-59
  return date;
}