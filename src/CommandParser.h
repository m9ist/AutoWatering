#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H
#include <Arduino.h>

// Чистая логика разбора telegram-команд. Вынесена из EspMain.cpp,
// чтобы покрываться native-тестами (test/test_command_parser).

inline bool isValidInteger(const String& str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}

// Парсит команду вида "<prefix> plantX Yml", например "/water plant3 50ml".
// true — если распарсилось, id и amount заполнены.
// Только формат: границы значений (id < 16, amount <= 200) проверяет
// вызывающий код. Длина числа ограничена 6 цифрами — защита от
// переполнения toInt/int (65536 иначе превращался бы в id=0).
inline bool parsePlantAmountCommand(const String& message, const String& prefix,
                                    int& id, int& amount) {
  const unsigned int MAX_NUMBER_DIGITS = 6;
  String expectedStart = prefix + F(" plant");
  if (!message.startsWith(expectedStart)) return false;
  if (!message.endsWith(F("ml"))) return false;
  int spacePos = message.indexOf(' ', expectedStart.length());
  if (spacePos < 0) return false;
  String plantId = message.substring(expectedStart.length(), spacePos);
  String amountStr = message.substring(spacePos + 1, message.length() - 2);
  if (!isValidInteger(plantId) || !isValidInteger(amountStr)) return false;
  if (plantId.length() > MAX_NUMBER_DIGITS ||
      amountStr.length() > MAX_NUMBER_DIGITS) {
    return false;
  }
  id = plantId.toInt();
  amount = amountStr.toInt();
  return true;
}

#endif  // COMMAND_PARSER_H
