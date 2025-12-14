#ifndef Communication_h
#define Communication_h

// #include "Stream.h"
// #include "WString.h"
#include <Arduino.h>
#include <Timer.h>

#define DEBUG_LOG

class Communication {
 protected:
#define STATE_START_COMMUNICATION_SEND 1
#define STATE_SEND 2
#define STATE_AWAIT 3
#define STATE_READ 4
#define STATE_TIMEOUT 5
#define STATE_FAILED 6
#define STATE_INIT 7
#define COMMUNICATION_OUT_MESSAGES_LENGTH 10
#define COMMUNICATION_IN_MESSAGES_LENGTH 2
#define COMMUNICATION_TIME_OUT_CTN 500
#define COMMUNICATION_TIME_OUT 10
#define COMMUNICATION_CHUNK_END '\t'
#define COMMUNICATION_MESSAGE_END "\n"
#define COMMUNICATION_START "start"
#define COMMUNICATION_READY "ready"
#define COMMUNICATION_NEXT "next"
#define COMMUNICATION_END "end"
#define COMMUNICATION_HELLOW "hellow world!\n"
#define COMMUNICATION_DATA_CHUNK_SIZE 62  // SERIAL_TX_BUFFER_SIZE

  int state = STATE_INIT;
  // если нужно закинуть в очередь сообщение, мы увеличиваем queueSize и
  // закидываем на место queuePos % COMMUNICATION_MESSAGES_LENGTH сообщение.
  // Если очередь полная, то мы просто перезатираем сообщение
  String queueRead[COMMUNICATION_IN_MESSAGES_LENGTH];
  int queueReadSize = 0;
  // текущая позиция первого сообщения в очереди, когда сообщение вычитывается,
  // мы смещаем позицию и обнуляем элемент
  int queueReadPos = 0;

  String queueWrite[COMMUNICATION_OUT_MESSAGES_LENGTH];
  int queueWriteSize = 0;
  int queueWritePos = 0;
  Stream& serial;
  Print& log;
  bool _readFirst;
  Timer timerHellowWorldSend;
  const Duration intervalHellowWorldSend = Timer::Seconds(5);

  String readNextChunk();
  void printChunk(String message);

  void readMessageAfterCommunicationStart();
  bool timeOut(String ret) {
    if (state == COMMUNICATION_TIME_OUT) {
#ifdef DEBUG_LOG
      if (ret.length() > 0) {
        log.print(F("Got timeout status with message: "));
        log.println(ret);
      } else {
        log.println(F("Got timeout status"));
      }
#endif
      return true;
    }
    return false;
  }

  void printCommunicationStart();

 public:
  Communication(Stream& communicationSerial, Print& logger, bool readFirst)
      : serial(communicationSerial), log(logger), _readFirst(readFirst) {
    timerHellowWorldSend.setDuration(intervalHellowWorldSend);
  }
  ~Communication() {}

  void communicationTick();
  void communicationSendMessage(String message);
  bool communicationHasMessage() { return queueReadSize > 0; }
  String communicationGetMessage();
};

#endif
