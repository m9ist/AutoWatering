#ifndef AW_LOGGING
#define AW_LOGGING
#include "SdFat.h"
#include "State.h"

#define SD_TURNED_OFF true

#define PIN_BUZZER 47

#define SD_CS_PIN A15
#define SD_MISO_PIN A12
#define SD_MOSI_PIN A13
#define SD_SCK_PIN A14

class AwLogging {
 private:
  FsFile file;
  bool _sdIsInited = false;

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
    SoftSpiDriver<SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN> softSpi;
    delay(500);
    SdSpiConfig sd_config =
        SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi);
    delay(100);
    SdFs sd;
    if (!sd.begin(sd_config)) {
      sd.initErrorPrint();
      return;
    }

    _sdIsInited = true;
    state.sdInited = true;
    Serial.println(F("SD is inited."));
  }

  void logFreeRam(int freeRam) {
    snprintf(freeRamLogginBuffer, sizeof(freeRamLogginBuffer), "Free RAM: %d",
             freeRam);
    Serial.println(freeRamLogginBuffer);
    if (SD_TURNED_OFF || !_sdIsInited) {
      return;
    }

    if (!file.open(F("LOG.txt"), FILE_WRITE)) {
      Serial.println(F("open file failed"));
    } else {
      file.println(freeRamLogginBuffer);
      file.close();
    }
  }

  void writeln(String dataString) {
    Serial.println(dataString);
    if (SD_TURNED_OFF || !_sdIsInited) {
      return;
    }

    if (!file.open(F("LOG.txt"), FILE_WRITE)) {
      Serial.println(F("open file failed"));
    } else {
      file.println(dataString);
      file.close();
    }
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