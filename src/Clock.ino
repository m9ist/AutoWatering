/** GetDateTime.cpp
 *
 * Example of getting the date and time from the RTC.
 *
 * @version 1.0.1
 * @author Rafa Couto <caligari@treboada.net>
 * @license GNU Affero General Public License v3.0
 * @see https://github.com/Treboada/Ds1302
 *
 */
#include <Arduino.h>
#include <Ds1302.h>

#define PIN_ENA 6
#define PIN_CLK 8
#define PIN_DAT 7

// DS1302 RTC instance
Ds1302 rtc(PIN_ENA, PIN_CLK, PIN_DAT);

const static char* WeekDays[] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                 "Friday", "Saturday", "Sunday"};

void initClock() {
  writeln("Init clock");
  // initialize the RTC
  rtc.init();
  writeln("Clock inited");

  // test if clock is halted and set a date-time (see example 2) to start it
  if (false) {
    writeln("RTC is halted. Setting time...");
    // rtc.halt();
    Ds1302::DateTime dt = {
        .year = 25,
        .month = Ds1302::MONTH_SET,  // в библиотеке опечатка, новая версия не
                                     // подхватывается
        .day = 8,
        .hour = 15,
        .minute = 13,
        .second = 00,
        .dow = Ds1302::DOW_MON};

    rtc.setDateTime(&dt);
    // rtc.start();
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
