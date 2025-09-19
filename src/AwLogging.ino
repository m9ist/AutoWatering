
#include "SdFat.h"

// Chip select may be constant or RAM variable.
const uint8_t SD_CS_PIN = 30;
//
// Pin numbers in templates must be constants.
const uint8_t SOFT_MISO_PIN = 26;
const uint8_t SOFT_MOSI_PIN = 28;
const uint8_t SOFT_SCK_PIN = 24;

// SdFat software SPI template
SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;
// Speed argument is ignored for software SPI.
// #if ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi)
// #else // ENABLE_DEDICATED_SPI
// #define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(0), &softSpi)
// #endif // ENABLE_DEDICATED_SPI

SdFs sd;
FsFile file;

#define SD_TURNED_OFF true

bool _inited = false;
int _sdPin;
// Sd2Card card;

void initLogging(int sdPin) {
  Serial.begin(9600);
  _sdPin = sdPin;
  Serial.println("Ininited logging...");
  if (SD_TURNED_OFF) {
    return;
  }
  if (true) {
    // пробуем sdFat software spi
    if (!sd.begin(SD_CONFIG)) {
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