#include "SdFat.h"

#define SD_TURNED_OFF false

#define PIN_BUZZER 47

#define SD_CS_PIN A15
#define SD_MISO_PIN A12
#define SD_MOSI_PIN A13
#define SD_SCK_PIN A14

SdFs sd;
FsFile file;
bool _sdIsInited = false;

void initLogging() {
  buzzerBoot();
  Serial.println("Ininited logging...");
  if (SD_TURNED_OFF) {
    return;
  }
  // пробуем sdFat software spi
  SoftSpiDriver<SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN> softSpi;
  SdSpiConfig sd_config =
      SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi);
  if (!sd.begin(sd_config)) {
    sd.initErrorPrint();
    return;
  }

  _sdIsInited = true;
  Serial.println(F("SD is inited."));
}

void writeln(String dataString) {
  Serial.println(dataString);
  if (SD_TURNED_OFF || !_sdIsInited) {
    return;
  }

  if (!file.open("LOG.txt", FILE_WRITE)) {
    Serial.println(F("open file failed"));
  } else {
    file.println(dataString);
    file.close();
  }
}

// int startup_melody[] = {784, 659, 523, 392};
// int startup_note_durations[] = {300, 300, 400, 500};  // Последняя нота
// длиннее
int startup_melody[] = {262, 330, 392, 523};
int startup_note_durations[] = {400, 400, 400, 300};

void buzzerBoot() {
  Serial.println("Startup melody");
  for (int i = 0; i < 4; i++) {
    tone(PIN_BUZZER, startup_melody[i], startup_note_durations[i]);
    delay(startup_note_durations[i]);
    delay(30);
  }
}

void buzzerCommand() {
  Serial.println("Command melody");
  tone(PIN_BUZZER, 1500, 200);
  delay(230);
  tone(PIN_BUZZER, 1000, 200);
  delay(230);
}