#include <SD.h>

bool _inited = false;

void initLogging(int sdPin) {
  Serial.begin(9600);
  pinMode(sdPin, OUTPUT);
  // Проверяем доступность карты
  _inited = SD.begin(sdPin); // todo обернуть в повторяющуся фигню
	if (!_inited) {
			Serial.println("SD init card failure!");
			return;
	}
	Serial.println("SD card is ready");
}
 
void writeln(String dataString) {
	Serial.println(dataString);
  File logFile = SD.open("LOG.txt", FILE_WRITE);
  if (logFile) {
		logFile.println(dataString);
		logFile.close();
	} else {
		Serial.println("Couldn't open log file");
	}
}