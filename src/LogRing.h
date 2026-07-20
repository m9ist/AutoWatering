#ifndef LOG_RING_H
#define LOG_RING_H

#include <cstddef>
#include <cstdint>

// Кольцевой байтовый буфер для строк лога MQTT-стока (issue #16): пока связь
// с брокером не установлена, EspLogger складывает готовые (уже JSON-
// сформированные) строки сюда, после реконнекта они досылаются по одной.
// Чисто C++, без Arduino-зависимостей — тестируется native
// (test/test_log_ring).
//
// Формат хранения: каждая строка — 2-байтовый заголовок длины (uint16_t,
// little-endian) + сами байты строки, друг за другом по кольцу.
class LogRing {
 public:
  explicit LogRing(size_t capacity)
      : buf_(new uint8_t[capacity]), capacity_(capacity) {}
  ~LogRing() { delete[] buf_; }

  // буфер один на объект, копия была бы двойным delete — запрещаем
  LogRing(const LogRing&) = delete;
  LogRing& operator=(const LogRing&) = delete;

  // Кладёт строку в кольцо. Если она сама по себе не влезает в буфер
  // целиком — отбрасывается и считается потерянной. Если места не хватает
  // из-за уже лежащих строк — вытесняются старейшие целиком (тоже в счётчик
  // потерь), пока строка не влезет.
  void push(const char* data, size_t len) {
    size_t total = kHeaderSize + len;
    if (total > capacity_) {
      lost_++;
      return;
    }
    while (used_ + total > capacity_ && count_ > 0) {
      dropOldest();
    }
    size_t pos = writeAt(tail_, len);
    pos = writeAt(pos, reinterpret_cast<const uint8_t*>(data), len);
    tail_ = pos;
    used_ += total;
    count_++;
  }

  // true, если в кольце нет ни одной строки
  bool empty() const { return count_ == 0; }

  // число строк, ждущих досылки
  size_t size() const { return count_; }

  // Достаёт самую старую строку в out (до maxLen байт, без завершающего
  // нуля), удаляя её из кольца. Возвращает фактически скопированную длину,
  // либо 0, если кольцо пусто. Строка в кольце всегда потребляется целиком,
  // даже если maxLen меньше её длины (лишний хвост просто не копируется).
  size_t pop(char* out, size_t maxLen) {
    if (count_ == 0) return 0;
    uint16_t len;
    size_t pos = readLenAt(head_, len);
    size_t copyLen = len < maxLen ? len : maxLen;
    size_t p = pos;
    for (size_t i = 0; i < copyLen; i++) {
      out[i] = static_cast<char>(buf_[p]);
      p = (p + 1) % capacity_;
    }
    head_ = (pos + len) % capacity_;
    used_ -= (kHeaderSize + len);
    count_--;
    return copyLen;
  }

  // Читает самую старую строку в out (до maxLen байт), НЕ удаляя её из
  // кольца. Возвращает ПОЛНУЮ длину строки (может быть больше maxLen —
  // вызывающий сам решает судьбу невлезающей строки), либо 0, если кольцо
  // пусто. Пара peek/dropFront вместо pop позволяет удалять строку только
  // после успешной отправки — FIFO-порядок не ломается при сбое.
  size_t peek(char* out, size_t maxLen) const {
    if (count_ == 0) return 0;
    uint16_t len;
    size_t pos = readLenAt(head_, len);
    size_t copyLen = len < maxLen ? len : maxLen;
    size_t p = pos;
    for (size_t i = 0; i < copyLen; i++) {
      out[i] = static_cast<char>(buf_[p]);
      p = (p + 1) % capacity_;
    }
    return len;
  }

  // Удаляет самую старую строку (обычно после peek). countAsLost=true —
  // строка не была доставлена (например, не влезла в буфер вызывающего)
  // и должна попасть в счётчик потерь.
  void dropFront(bool countAsLost) {
    if (count_ == 0) return;
    uint16_t len;
    size_t pos = readLenAt(head_, len);
    head_ = (pos + len) % capacity_;
    used_ -= (kHeaderSize + len);
    count_--;
    if (countAsLost) lost_++;
  }

  // сколько строк потеряно (вытеснены переполнением или не влезли целиком)
  // с последнего resetLostCount()
  uint32_t lostCount() const { return lost_; }
  void resetLostCount() { lost_ = 0; }

 private:
  static const size_t kHeaderSize = 2;

  uint8_t* buf_;
  size_t capacity_;
  size_t head_ = 0;  // позиция самой старой строки
  size_t tail_ = 0;  // позиция записи следующей строки
  size_t used_ = 0;  // занято байт (данные + заголовки длины)
  size_t count_ = 0;
  uint32_t lost_ = 0;

  // пишет 2-байтовый little-endian заголовок длины, возвращает позицию
  // после записи
  size_t writeAt(size_t pos, uint16_t len) {
    buf_[pos] = static_cast<uint8_t>(len & 0xFF);
    pos = (pos + 1) % capacity_;
    buf_[pos] = static_cast<uint8_t>((len >> 8) & 0xFF);
    return (pos + 1) % capacity_;
  }
  size_t writeAt(size_t pos, const uint8_t* src, size_t n) {
    for (size_t i = 0; i < n; i++) {
      buf_[pos] = src[i];
      pos = (pos + 1) % capacity_;
    }
    return pos;
  }
  // читает 2-байтовый заголовок длины начиная с pos, возвращает позицию
  // сразу после него (начало данных строки)
  size_t readLenAt(size_t pos, uint16_t& outLen) const {
    uint8_t lo = buf_[pos];
    pos = (pos + 1) % capacity_;
    uint8_t hi = buf_[pos];
    outLen = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
    return (pos + 1) % capacity_;
  }

  void dropOldest() { dropFront(/*countAsLost=*/true); }
};

#endif  // LOG_RING_H
