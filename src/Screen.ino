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

#include "ST7789_AVR.h"

#define CS_ALWAYS_LOW
#define COMPATIBILITY_MODE

#define TFT_DC 39
#define TFT_RES 37
#define TFT_CS -1

// размеры экрана
#define SCR_WD 240
#define SCR_HT 240

ST7789_AVR lcd = ST7789_AVR(TFT_DC, TFT_RES, TFT_CS);

void initScreen() {
  writeln("Before lcd init");

  lcd.init(SCR_WD, SCR_HT);

  lcd.fillScreen(RED);
  lcd.setTextColor(WHITE, BLUE);
  lcd.setTextSize(2);
  lcd.setCursor(0, 0);
  lcd.println("Hellow world");
  lcd.setCursor(0, 18);
}

void loopScreen() {
  lcd.fillScreen(RED);
  lcd.setTextColor(WHITE, BLUE);
  lcd.setTextSize(2);
  lcd.setCursor(0, 0);
  lcd.println("Hellow world");
  lcd.setCursor(0, 18);
  lcd.print("SD card is ");
  lcd.println(_sdIsInited);
  lcd.setCursor(0, 36);
  lcd.println(getDateAndTime());
}
