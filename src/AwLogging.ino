#include "SdFat.h"

#define SD_TURNED_OFF false

#define PIN_BUZZER 47

#define SD_CS_PIN A15
#define SD_MISO_PIN A12
#define SD_MOSI_PIN A13
#define SD_SCK_PIN A14

SdFs sd;
FsFile file;

bool _inited = false;
// Sd2Card card;

void initLogging() {
  buzzerBoot();
  Serial.println("Ininited logging...");
  if (SD_TURNED_OFF) {
    return;
  }
  if (true) {
    // пробуем sdFat software spi
    SoftSpiDriver<SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN> softSpi;
    SdSpiConfig sd_config =
        SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi);
    if (!sd.begin(sd_config)) {
      sd.initErrorHalt();
    }

    if (!file.open("SoftSPI.txt", O_RDWR | O_CREAT)) {
      sd.errorHalt(F("open failed"));
    }
    file.println(F("This line was printed using software SPI."));

    file.rewind();

    while (file.available()) {
      Serial.write(file.read());
    }

    file.close();

    Serial.println(F("Done."));
    return;
  }
  ///---------------------------------------------------------------------------------------------------------
  /*
          if (!card.init(SPI_HALF_SPEED, _sdPin))
          {
                  Serial.println("initialization failed. Things to check:");
                  Serial.println("* is a card inserted?");
                  Serial.println("* is your wiring correct?");
                  Serial.println("* did you change the chipSelect pin to match
     your shield or module?"); Serial.println("Note: press reset button on the
     board and reopen this Serial Monitor after fixing your issue!");
                  // while (1);
          }
          else
          {
                  Serial.println("Wiring is correct and a card is present.");
                  _inited = true;
          }

          // print the type of card
          Serial.println();
          Serial.print("Card type:         ");
          switch (card.type())
          {
          case SD_CARD_TYPE_SD1:
                  Serial.println("SD1");
                  break;
          case SD_CARD_TYPE_SD2:
                  Serial.println("SD2");
                  break;
          case SD_CARD_TYPE_SDHC:
                  Serial.println("SDHC");
                  break;
          default:
                  Serial.println("Unknown");
          }

          // Now we will try to open the 'volume'/'partition' - it should be
     FAT16 or FAT32 SdVolume volume; if (!volume.init(card))
          {
                  Serial.println("Could not find FAT16/FAT32 partition.\nMake
     sure you've formatted the card");
                  // while (1);
          }

          Serial.print("Clusters:          ");
          Serial.println(volume.clusterCount());
          Serial.print("Blocks x Cluster:  ");
          Serial.println(volume.blocksPerCluster());

          Serial.print("Total Blocks:      ");
          Serial.println(volume.blocksPerCluster() * volume.clusterCount());
          Serial.println();

          // print the type and size of the first FAT-type volume
          uint32_t volumesize;
          Serial.print("Volume type is:    FAT");
          Serial.println(volume.fatType(), DEC);
          ///---------------------------------------------------------------------------------------------------------

          Serial.println("Reinit SD");
          if (SD.begin(_sdPin))
          {
                  Serial.println("Reinit SD succ!");
          }
          else
          {
                  Serial.println("Reinit SD failed");
          }
          // pinMode(sdPin, OUTPUT);
          // digitalWrite(_sdPin, HIGH);
          // // Проверяем доступность карты
          // _inited = SD.begin(sdPin); // todo обернуть в повторяющуся фигню
          // if (!_inited) {
          // 		Serial.println("SD unit init failure!");
          // 		return;
          // }
          // digitalWrite(_sdPin, LOW);
          */
}

void writeln(String dataString) {
  Serial.println(dataString);
  if (SD_TURNED_OFF || !_inited) {
    return;
  }
  /*
  // digitalWrite(_sdPin, HIGH);
  File logFile = SD.open("LOG.txt", FILE_WRITE);
  if (logFile)
  {
          logFile.println(dataString);
          logFile.close();
          // digitalWrite(_sdPin, LOW);
  }
  else
  {
          Serial.println("Couldn't open log file");
  }
          */
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