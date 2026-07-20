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
  virtual bool publish(const String& json) = 0;
  virtual ~ILogSink() {}
};

// Логгер за прежним интерфейсом (наследует Print, как раньше sets::Logger из
// SettingsGyver) — им пользуются Communication.h и PointsHoler/Graph
// (Graph.h). Сток — MQTT (issue #16): готовые строки склеиваются построчно
// (по '\n', как раньше делал println) и оборачиваются в JSON
// {"ts","lvl","msg"}. При живом соединении публикуются немедленно; при
// обрыве — копятся в RAM-кольце (LogRing) и досылаются после реконнекта
// (см. flushPending/onMqttReconnected, дёргаются из EspMain::mqttLoop).
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

  // Вызывать из EspMain при обнаружении фронта реконнекта (было
  // disconnected, стало connected): если за время обрыва что-то потерялось
  // из кольца, публикует маркер "reconnected, N lines lost" и сбрасывает
  // счётчик потерь. Строки, которые остались в кольце (не потерялись),
  // досылаются отдельно через flushPending().
  void onMqttReconnected() {
    uint32_t lost = ring_.lostCount();
    if (lost == 0 || sink_ == nullptr) return;
    ring_.resetLostCount();
    String msg = (String)F("reconnected, ") + lost + F(" lines lost");
    sink_->publish(buildLogJson(msg, F("error")));
  }

  // Досылает из кольца не больше maxLines строк за вызов — не блокирует
  // loop(), вызывать раз в тик, когда sink_->connected().
  void flushPending(uint8_t maxLines) {
    if (sink_ == nullptr || !sink_->connected()) return;
    // запас под JSON-обёртку и экранирование кавычек в msg (kMaxLineLen —
    // это длина сырой строки до экранирования)
    char buf[512];
    for (uint8_t i = 0; i < maxLines && !ring_.empty(); i++) {
      size_t len = ring_.pop(buf, sizeof(buf) - 1);
      if (len == 0) break;
      buf[len] = '\0';
      if (!sink_->publish(String(buf))) {
        // не смогли отправить — вернём строку в кольцо и прервём досылку
        // в этом тике (см. комментарий класса про рекурсию — не логируем)
        ring_.push(buf, len);
        break;
      }
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
      if (sink_->publish(json)) return;
      // publish() отказал, хотя формально были на связи — падаем в кольцо,
      // без лога об этом (см. комментарий класса)
    }
    ring_.push(json.c_str(), json.length());
  }
};

#endif  // ESP_LOGGER_H
