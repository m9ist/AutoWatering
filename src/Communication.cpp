#include "Communication.h"

// \0 null \r \a \b \f \v
// https://forum.arduino.cc/t/printing-special-characters/97446

String Communication::readNextChunk() {
  int ctn = 0;
  String ret;
  while (ctn < COMMUNICATION_TIME_OUT_CTN) {
    if (!serial.available()) {
      delay(COMMUNICATION_TIME_OUT);
      ctn++;
      continue;
    }
    int read = serial.read();
    if (read == COMMUNICATION_CHUNK_END) {
      if (ret.length() == 0) {
        // такое возможно для обнуления "мусора", которое шло до этого
        continue;
      } else {
        // log.print(F("Got chunk"));
        // log.println(ret);
        return ret;
      }
    }
    ret += (char)read;
  }
  if (ctn == COMMUNICATION_TIME_OUT_CTN) {
    log.print(F("got communication time out on "));
    log.println(ret);
    state = STATE_TIMEOUT;
  }
  return ret;
}

void Communication::printChunk(String message) {
  // log.print(F("Send message"));
  // log.println(message);
  serial.print(message);
  serial.print(COMMUNICATION_CHUNK_END);
}

void Communication::printCommunicationStart() {
  // log.println(F("Communication start"));
  serial.print(COMMUNICATION_CHUNK_END);
  serial.print(COMMUNICATION_START);
  serial.print(COMMUNICATION_CHUNK_END);
}

void Communication::readMessageAfterCommunicationStart() {
  printChunk(COMMUNICATION_READY);
  String result;
  while (state == STATE_READ) {
    String ret = readNextChunk();
    if (timeOut(ret)) {
      return;
    } else if (ret == COMMUNICATION_MESSAGE_END) {
      printChunk(COMMUNICATION_END);
      break;
    } else {
      printChunk(COMMUNICATION_NEXT);
    }
    result += ret;
  }

  // закидываем в входящую очередь
  if (state == STATE_READ) {
    int pos;
    if (queueReadSize == COMMUNICATION_IN_MESSAGES_LENGTH) {
      // перезатираем сообщение, которое стоит первым со сдвигом позиции
      pos = queueReadPos;
      queueReadPos = (queueReadPos + 1) % COMMUNICATION_IN_MESSAGES_LENGTH;
    } else {
      // высчитываем позицию после последнего элемента в очереди
      pos = (queueReadPos + queueReadSize) % COMMUNICATION_IN_MESSAGES_LENGTH;
      queueReadSize++;
    }
    result.trim();
    queueRead[pos] = result;
    state = STATE_AWAIT;
  }
}

// Все коммуникации начинаются с COMMUNICATION_START, в ответ летит
// COMMUNICATION_READY, после чего идет общение до COMMUNICATION_END
// Если одновременно оба источника пытаются стартануть общение, то разруливает
// флаг _readFirst, кто "прогинается, читает общение", а отправляющий инорирует
// старт комуникации
void Communication::communicationTick() {
  //************ произошел тайм аут, посылаем привет и вычитываем все до привета
  if (state != STATE_AWAIT) {
    if (timerHellowWorldSend.expired()) {
      log.println((String)F("Send hellow world, read queue=") + queueReadSize +
                  F(" write queue=") + queueWriteSize + F(" state=") + state);
      serial.print(COMMUNICATION_HELLOW);
      timerHellowWorldSend.setDuration(intervalHellowWorldSend);
    }
    String ret;
    int lookStartIdPos = 0;
    // наша задача вычитывать весь доступный буфер до старта коммуникаций
    for (int i = 0; i < 3; i++) {
      while (serial.available() > 0) {
        int read = serial.read();

        if (COMMUNICATION_HELLOW[lookStartIdPos] == read) {
          //   log.print(lookStartIdPos);
          //   log.print('/');
          //   log.print(sizeof(COMMUNICATION_HELLOW));
          //   log.print("=");
          //   log.print(ret + (char)read);
          //   log.print(";");
          lookStartIdPos++;
        } else {
          //   if (lookStartIdPos > 0) {
          //     log.print(ret);
          //     log.print("!!failed");
          //   }
          lookStartIdPos = 0;
        }
        // здесь -1, потому что "\n" при подсчете size идет как 2 символа...
        if (lookStartIdPos == sizeof(COMMUNICATION_HELLOW) - 1) {
          // отловили старт коммуникаций
          state = STATE_AWAIT;
          log.println(F("Got hellow world, commucation enter normal mode."));
          return;
        }
        if (read == '\n' && lookStartIdPos == 0) {
          ret.trim();
          if (ret.length() > 0) {
            log.print(F("State = "));
            log.println(state);
            log.print(F("Got some noise in serial: "));
            log.println(ret);
          }
          return;
        }
        ret += (char)read;
      }
      delay(10);
    }
    ret.trim();
    if (ret.length() > 0) {
      log.print(F("State = "));
      log.println(state);
      log.print(F("Got some noise in serial: "));
      log.println(ret);
    }
    return;
  } else {
    if (queueReadSize > 0 || queueWriteSize > 0) {
      log.println((String)F("read queue=") + queueReadSize +
                  F(" write queue=") + queueWriteSize + F(" state=") + state);
    }
  }

  //************ сначала вычитываем сообщения на вход
  if (state == STATE_AWAIT && serial.available() > 0) {
    state = STATE_READ;
    String ret = readNextChunk();
    if (timeOut(ret)) return;
    if (ret != COMMUNICATION_START) {
      log.print(F("Got unexpected (not start) message "));
      log.println(ret);
      state = STATE_FAILED;
      return;
    }
    readMessageAfterCommunicationStart();
    return;
  }

  //************ теперь все отсылаем
  if (state == STATE_AWAIT && queueWriteSize > 0) {
    state = STATE_SEND;

    // для начала стартуем коммуникации, если получается, то начинаем общение
    // для простоты мы просылаем лишнюю табуляцию
    printCommunicationStart();
    String ret = readNextChunk();
    if (timeOut(ret)) return;
    // сначала проверим конфликт ситуации, когда оба пытаются стартануть общение
    if (ret == COMMUNICATION_START) {
      log.println(F("Got conflict on communication start"));
      if (_readFirst) {
        // если мы прогинаемся, то заходим в функцию чтения сообщения
        state = STATE_READ;
        log.println(F("Enter read mode"));
        readMessageAfterCommunicationStart();
        return;
      } else {
        // если же мы гнем свою линию, то мы снова читаем следующий токен (нода
        // "прогинается", поэтому она должна прислать готовность читать)
        log.println(F("reread next token, await read"));
        ret = readNextChunk();
        if (timeOut(ret)) return;
      }
    }
    if (ret != COMMUNICATION_READY) {
      state = STATE_FAILED;
      return;
    }

    String message = queueWrite[queueWritePos];
    queueWrite[queueWritePos] = F("");
    queueWritePos = (queueWritePos + 1) % COMMUNICATION_OUT_MESSAGES_LENGTH;
    queueWriteSize--;

    unsigned int nextMessagePartStart = 0;
    while (nextMessagePartStart < message.length()) {
      String chunk = message.substring(
          nextMessagePartStart,
          min(message.length(), nextMessagePartStart + COMMUNICATION_DATA_CHUNK_SIZE));
      nextMessagePartStart += COMMUNICATION_DATA_CHUNK_SIZE;
      printChunk(chunk);
      ret = readNextChunk();
      if (timeOut(ret)) return;
      if (ret != COMMUNICATION_NEXT) {
        state = STATE_FAILED;
        return;
      }
    }
    printChunk(COMMUNICATION_MESSAGE_END);
    ret = readNextChunk();
    if (timeOut(ret)) return;
    if (ret == COMMUNICATION_END) {
      state = STATE_AWAIT;
    } else {
      log.print(F("State failed after message send, got unexpected message: "));
      log.println(ret);
      state = STATE_FAILED;
    }

    return;
  }
}

// добавляем в исходящую очередь сообщений на отправку
void Communication::communicationSendMessage(String message) {
  int pos;
  if (queueWriteSize == COMMUNICATION_OUT_MESSAGES_LENGTH) {
    // перезатираем сообщение, которое стоит первым со сдвигом позиции
    log.print(F("Full queue, rewrite first element: "));
    log.println(queueWrite[queueWritePos]);
    pos = queueWritePos;
    queueWritePos = (queueWritePos + 1) % COMMUNICATION_OUT_MESSAGES_LENGTH;
  } else {
    // высчитываем позицию после последнего элемента в очереди
    pos = (queueWritePos + queueWriteSize) % COMMUNICATION_OUT_MESSAGES_LENGTH;
    queueWriteSize++;
  }
  queueWrite[pos] = message;
  // log.println((String)F("Queue size ") + queueWriteSize + F(" pos=") + pos +
  // F(" writePos") + queueWritePos + F(" message ") +
  // queueWrite[pos]);
}

// выдаем сообщение из входящей очереди сообщений
String Communication::communicationGetMessage() {
  String message = queueRead[queueReadPos];
  queueRead[queueReadPos] = F("");
  queueReadPos = (queueReadPos + 1) % COMMUNICATION_IN_MESSAGES_LENGTH;
  queueReadSize--;
  return message;
}