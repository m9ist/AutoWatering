#ifndef ESP_LOGGER_H
#define ESP_LOGGER_H
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LogRing.h>

// Интерфейс MQTT-стока для EspLogger — реализация (обёртка над PubSubClient)
// живёт в EspMain.cpp, чтобы EspLogger.h не тянул зависимость от
// PubSubClient. Инжектируется через setSink().
class ILogSink {
 public:
  virtual bool connected() = 0;
  virtual bool publish(const char* json) = 0;
  virtual ~ILogSink() {}
};

// Логгер за прежним интерфейсом (наследует Print, как раньше sets::Logger из
// SettingsGyver) — им пользуется Communication.h.
// Сток — MQTT (issue #16): готовые строки склеиваются построчно
// (по '\n', как раньше делал println) и оборачиваются в JSON
// {"ts","lvl","msg"}. При живом соединении публикуются немедленно; при
// обрыве — копятся в RAM-кольце (LogRing) и досылаются после реконнекта
// (см. flushPending, дёргается из EspMain::mqttLoop).
//
// Важно: при неудачной попытке publish() эта неудача НЕ логируется через
// сам логгер — иначе лог о сбое публикации попытался бы опубликоваться
// тем же путём и мог зациклиться. Строка просто уходит в кольцо.
class EspLogger : public Print {
 public:
  // защита от неограниченного роста накопителя строки, если кто-то вызовет
  // print() много раз без завершающего println() (в норме такого не бывает)
  static const size_t kMaxLineLen = 240;
  // размер RAM-кольца для строк, ждущих реконнекта (issue #16: ~2-4 КБ)
  static const size_t kRingCapacity = 3072;
  // буфер досылки: kMaxLineLen после JSON-экранирования (×2) + обёртка
  static const size_t kFlushBufLen = 640;

  EspLogger() : ring_(kRingCapacity) {}

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      char c = (char)buffer[i];
      if (c == '\n') {
        handleLine(line_);
        line_ = String();
      } else if (c != '\r') {
        if (line_.length() < kMaxLineLen) {
          line_ += c;
        }
      }
    }
    return size;
  }

  void setSink(ILogSink* sink) { sink_ = sink; }
  // provider возвращает текущее время в формате, которым помечаются строки
  // (см. getTimestamp() в EspMain.cpp)
  void setTimestampProvider(String (*provider)()) {
    timestampProvider_ = provider;
  }

  // Досылает из кольца не больше maxLines строк за вызов — не блокирует
  // loop(), вызывать раз в тик, когда sink_->connected(). Сначала — маркер
  // потерь (если были), потом строки кольца.
  void flushPending(uint8_t maxLines) {
    if (sink_ == nullptr || !sink_->connected()) return;
    // Маркер публикуем раньше строк кольца: потерянные (вытесненные) строки
    // старше оставшихся, так хронология в Loki сохраняется. Счётчик
    // сбрасываем только после подтверждённой публикации — иначе при сбое
    // publish маркер пропал бы насовсем.
    uint32_t lost = ring_.lostCount();
    if (lost > 0) {
      String msg = (String)F("reconnected, ") + lost + F(" lines lost");
      if (!sink_->publish(buildLogJson(msg, F("error")).c_str())) return;
      ring_.resetLostCount();
    }
    // запас: kMaxLineLen сырых символов после JSON-экранирования (худший
    // случай ×2) + обёртка {"ts","lvl","msg"} — до ~540 байт
    char buf[kFlushBufLen];
    for (uint8_t i = 0; i < maxLines && !ring_.empty(); i++) {
      size_t fullLen = ring_.peek(buf, sizeof(buf) - 1);
      if (fullLen == 0) break;
      if (fullLen > sizeof(buf) - 1) {
        // строка не влезает в буфер досылки (при kMaxLineLen=240 не должно
        // случаться) — обрезанный битый JSON не публикуем, считаем потерей
        ring_.dropFront(/*countAsLost=*/true);
        continue;
      }
      buf[fullLen] = '\0';
      // publish до удаления из кольца: при сбое строка остаётся головой
      // кольца — FIFO-порядок досылки не ломается (см. ревью #16)
      if (!sink_->publish(buf)) return;
      ring_.dropFront(/*countAsLost=*/false);
    }
  }

 private:
  String line_;
  LogRing ring_;
  ILogSink* sink_ = nullptr;
  String (*timestampProvider_)() = nullptr;

  String buildLogJson(const String& msg, const String& lvl) {
    JsonDocument doc;
    doc[F("ts")] = timestampProvider_ ? timestampProvider_() : String();
    doc[F("lvl")] = lvl;
    doc[F("msg")] = msg;
    String out;
    serializeJson(doc, out);
    return out;
  }

  void handleLine(const String& line) {
    if (line.length() == 0) return;
    String json = buildLogJson(line, F("info"));
    if (sink_ != nullptr && sink_->connected()) {
      if (sink_->publish(json.c_str())) return;
      // publish() отказал, хотя формально были на связи — падаем в кольцо,
      // без лога об этом (см. комментарий класса)
    }
    ring_.push(json.c_str(), json.length());
  }
};

#endif  // ESP_LOGGER_H
