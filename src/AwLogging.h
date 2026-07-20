#ifndef AW_LOGGING
#define AW_LOGGING
#include "SdFat.h"
#include "State.h"

#define SD_TURNED_OFF false

#define PIN_BUZZER 47

#define SD_CS_PIN A15
#define SD_MISO_PIN A12
#define SD_MOSI_PIN A13
#define SD_SCK_PIN A14

class AwLogging {
 private:
  SdFs sd;
  SoftSpiDriver<SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN> softSpi;
  FsFile file;
  bool _sdIsInited = false;
  unsigned long _lastFlushMs = 0;
  // Файл держим открытым, на карту сбрасываем раз в интервал: open/close
  // на каждую строку — постоянная перезапись FAT (износ карты + латенси
  // loop). Цена: при отвале питания теряется до 5с последних логов.
  static const unsigned long FLUSH_INTERVAL_MS = 5000;

  bool ensureFileOpen() {
    if (file) return true;
    file = sd.open("LOG.txt", FILE_WRITE);
    if (!file) {
      Serial.print(F("open file failed, sdErr=0x"));
      Serial.print(sd.sdErrorCode(), HEX);
      Serial.print(F(" data=0x"));
      Serial.println(sd.sdErrorData(), HEX);
      return false;
    }
    return true;
  }

  void flushIfNeeded() {
    if (millis() - _lastFlushMs < FLUSH_INTERVAL_MS) return;
    _lastFlushMs = millis();
    file.flush();
  }

  // int startup_melody[] = {784, 659, 523, 392};
  // int startup_note_durations[] = {300, 300, 400, 500};  // Последняя нота
  // длиннее
  int startup_melody[4] = {262, 330, 392, 523};
  int startup_note_durations[4] = {400, 400, 400, 300};
  char freeRamLogginBuffer[16];

  void buzzerBoot() {
    Serial.println(F("Startup melody"));
    for (int i = 0; i < 4; i++) {
      tone(PIN_BUZZER, startup_melody[i], startup_note_durations[i]);
      delay(startup_note_durations[i]);
      delay(30);
    }
  }

 public:
  AwLogging() {}
  ~AwLogging() {}

  void init(State& state) {
    buzzerBoot();
    if (SD_TURNED_OFF) {
      return;
    }
    Serial.println(F("Start init logging..."));
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    delay(500);
    SdSpiConfig sd_config(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(1), &softSpi);
    if (!sd.begin(sd_config)) {
      sd.initErrorPrint();
      return;
    }

    _sdIsInited = true;
    state.sdInited = true;
    ensureFileOpen();
    Serial.println(F("SD is inited."));
  }

  void logFreeRam(int freeRam) {
    snprintf(freeRamLogginBuffer, sizeof(freeRamLogginBuffer), "Free RAM: %d",
             freeRam);
    Serial.println(freeRamLogginBuffer);
    if (SD_TURNED_OFF || !_sdIsInited) {
      return;
    }
    if (!ensureFileOpen()) return;
    file.println(freeRamLogginBuffer);
    flushIfNeeded();
  }

  void writeln(String dataString) {
    Serial.println(dataString);
    if (SD_TURNED_OFF || !_sdIsInited) {
      return;
    }
    if (!ensureFileOpen()) return;
    file.println(dataString);
    flushIfNeeded();
  }

  void buzzerCommand() {
    Serial.println(F("Command melody"));
    tone(PIN_BUZZER, 1500, 200);
    delay(230);
    tone(PIN_BUZZER, 1000, 200);
    delay(230);
  }
};
#endif