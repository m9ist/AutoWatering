/*
ST7789 240x240 1.3" IPS (without CS pin) - only 4+2 wires required:
 #01 GND -> GND
 #02 VCC -> VCC (3.3V only!)
 #03 SCL -> D13/SCK
 #04 SDA -> D11/MOSI
 #05 RES -> D9 /PA0 or any digital (HW RESET is required to properly initialize
LCD without CS) #06 DC  -> D10/PA1 or any digital #07 BLK -> NC

*/

#include <Adafruit_GFX.h>
#include <SPI.h>

#include "AwLogging.h"
#include "ST7789_AVR.h"

#define CS_ALWAYS_LOW
#define COMPATIBILITY_MODE

#define TFT_DC 39
#define TFT_RES 37
#define TFT_CS -1

// размеры экрана
#define SCR_WD 240
#define SCR_HT 240

#define PIN_CHECK_BUTTON 33
#define PIN_UP_BUTTON 29
#define PIN_DOWN_BUTTON 31
#define PIN_MODE_BUTTON 27

ST7789_AVR lcd = ST7789_AVR(TFT_DC, TFT_RES, TFT_CS);

void initScreen(AwLogging& logger) {
  logger.writeln(F("Start display init"));
  pinMode(PIN_CHECK_BUTTON, INPUT_PULLUP);
  pinMode(PIN_UP_BUTTON, INPUT_PULLUP);
  pinMode(PIN_DOWN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);

  lcd.init(SCR_WD, SCR_HT);

  lcd.fillScreen(YELLOW);
  lcd.setTextColor(WHITE, BLUE);
  lcd.setTextSize(5);
  lcd.setCursor(25, 50);
  lcd.println(F("Hellow"));
  lcd.setCursor(35, 100);
  lcd.println(F("world"));
  delay(1000);
}

String lastMessage;

void drawScreenMessage(String message, AwLogging& logger) {
  if (lastMessage == message) return;
  logger.writeln(message);
  lastMessage = message;

  lcd.fillScreen(YELLOW);
  lcd.setTextColor(BLUE, YELLOW);
  lcd.setTextSize(4);
  lcd.setCursor(0, 0);
  lcd.print(message);
}

uint16_t getFont(bool isGood) { return BLACK; }

uint16_t getBackground(bool isGood) { return isGood ? GREEN : RED; }

// всего помещается 12 строчек, последняя с доп сдвигом на 5 пикселей
void loopScreen(State& globalState) {
#ifdef SD_TURNED_OFF
  bool screenState = true;
#elif
  bool screenState = globalState.sdInited;
#endif
  uint16_t screenBack = getBackground(screenState);
  uint16_t screenFont = getFont(screenState);
  lcd.fillScreen(screenBack);
  lcd.setTextColor(screenFont, screenBack);
  lcd.setTextSize(2);

  int stepx = 1;
  int stepy = 1;
  int lineHeight = 18;
  int lineId = 0;

  lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  lcd.println(dateToString(globalState.lastSensorCheck));
  lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  lcd.print((int)globalState.temperature);
  lcd.print((char)223);
  lcd.print(' ');
  lcd.print((int)globalState.humidity);
  lcd.println('%');

  lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  lcd.print(F("Free memory: "));
  lcd.println(globalState.freeMemorySize);

  // lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  // lcd.println(F("Next task runing"));
  // lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  // lcd.println(dateToString(globalState.nextTaskRuning));
  lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  lcd.println(F("Start app date"));
  lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  lcd.println(dateToString(globalState.startUpDate));

  String out = F("Sensors");
  for (int i = 0; i < 16; i++) {
    if (globalState.plants[i].isOn != PLANT_IS_ON) continue;
    out += ';';
    out += i;
    out += '=';
    out += globalState.plants[i].originalValue;
  }
  lcd.setCursor(stepx, stepy + lineHeight * lineId++);
  lcd.println(out);

  lcd.setCursor(stepx, stepy + 5 + lineHeight * 12);
  lcd.setTextColor(getFont(globalState.sdInited),
                   getBackground(globalState.sdInited));
  lcd.print(F("SD"));
  lcd.setTextColor(screenFont, screenBack);
  lcd.print(' ');
  lcd.setTextColor(getFont(globalState.pompIsOn),
                   getBackground(globalState.pompIsOn));
  lcd.print(F("pomp"));
  lcd.setTextColor(screenFont, screenBack);
  lcd.print(' ');
  lcd.setTextColor(getFont(globalState.espConnectedAndTimeSynced),
                   getBackground(globalState.espConnectedAndTimeSynced));
  lcd.print(F("esp"));
  lcd.setTextColor(screenFont, screenBack);
  lcd.print(' ');
  lcd.setTextColor(getFont(globalState.temperatureSensorInited),
                   getBackground(globalState.temperatureSensorInited));
  lcd.print(F("temp"));
  lcd.setTextColor(screenFont, screenBack);
  lcd.print(' ');
}

bool isCheckButtonPressed() {
  int v = digitalRead(PIN_CHECK_BUTTON);
  return v == LOW;
}

bool isUpButtonPressed() {
  int v = digitalRead(PIN_UP_BUTTON);
  return v == LOW;
}

bool isDownButtonPressed() {
  int v = digitalRead(PIN_DOWN_BUTTON);
  return v == LOW;
}

bool isModeButtonPressed() {
  int v = digitalRead(PIN_MODE_BUTTON);
  return v == LOW;
}