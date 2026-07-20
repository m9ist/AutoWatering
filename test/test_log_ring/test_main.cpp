#include <LogRing.h>
#include <cstring>
#include <string>
#include <unity.h>

void setUp() {}
void tearDown() {}

// вспомогалка: достаёт строку из кольца как std::string
std::string popStr(LogRing& ring, size_t maxLen = 64) {
  char buf[64];
  size_t len = ring.pop(buf, maxLen);
  return std::string(buf, len);
}

// --- пустое кольцо ---

void test_empty_ring_pop_returns_zero() {
  LogRing ring(64);
  TEST_ASSERT_TRUE(ring.empty());
  char buf[8];
  TEST_ASSERT_EQUAL(0, ring.pop(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT32(0, ring.lostCount());
}

// --- порядок FIFO при досылке ---

void test_fifo_order_preserved() {
  LogRing ring(256);
  ring.push("first", 5);
  ring.push("second", 6);
  ring.push("third", 5);
  TEST_ASSERT_EQUAL(3, ring.size());

  TEST_ASSERT_EQUAL_STRING("first", popStr(ring).c_str());
  TEST_ASSERT_EQUAL_STRING("second", popStr(ring).c_str());
  TEST_ASSERT_EQUAL_STRING("third", popStr(ring).c_str());
  TEST_ASSERT_TRUE(ring.empty());
  TEST_ASSERT_EQUAL_UINT32(0, ring.lostCount());
}

// --- переполнение: старьё выбрасывается целиком, счётчик потерь растёт ---

void test_overflow_drops_oldest_and_counts_loss() {
  // буфер вмещает 2 строки по 5 байт (2+5=7 каждая, 14 всего)
  LogRing ring(14);
  ring.push("aaaaa", 5);
  ring.push("bbbbb", 5);
  TEST_ASSERT_EQUAL(2, ring.size());

  // третья строка не влезает без вытеснения "aaaaa"
  ring.push("ccccc", 5);

  TEST_ASSERT_EQUAL(2, ring.size());
  TEST_ASSERT_EQUAL_UINT32(1, ring.lostCount());
  // осталась вторая и третья, в правильном порядке — первая вытеснена
  TEST_ASSERT_EQUAL_STRING("bbbbb", popStr(ring).c_str());
  TEST_ASSERT_EQUAL_STRING("ccccc", popStr(ring).c_str());
}

void test_overflow_counts_multiple_losses() {
  LogRing ring(14);  // 2 строки по 5 байт
  ring.push("11111", 5);
  ring.push("22222", 5);
  ring.push("33333", 5);  // вытесняет "11111"
  ring.push("44444", 5);  // вытесняет "22222"

  TEST_ASSERT_EQUAL_UINT32(2, ring.lostCount());
  TEST_ASSERT_EQUAL_STRING("33333", popStr(ring).c_str());
  TEST_ASSERT_EQUAL_STRING("44444", popStr(ring).c_str());
}

// --- маркер потерь: счётчик читается и сбрасывается (как перед публикацией
// "reconnected, N lines lost" в EspLogger) ---

void test_lost_count_reset_after_marker() {
  LogRing ring(14);
  ring.push("11111", 5);
  ring.push("22222", 5);
  ring.push("33333", 5);  // вытесняет "11111", lost=1

  TEST_ASSERT_EQUAL_UINT32(1, ring.lostCount());
  ring.resetLostCount();
  TEST_ASSERT_EQUAL_UINT32(0, ring.lostCount());

  // новое переполнение снова считается с нуля
  ring.push("44444", 5);
  TEST_ASSERT_EQUAL_UINT32(1, ring.lostCount());
}

// --- строка больше буфера целиком: не сохраняется, считается потерянной,
// остальное кольцо не трогается ---

void test_line_larger_than_buffer_is_dropped() {
  LogRing ring(16);
  ring.push("kept", 4);
  // 2 (заголовок) + 20 > 16 — не влезет никогда
  ring.push("this line is way too long for the ring", 39);

  TEST_ASSERT_EQUAL_UINT32(1, ring.lostCount());
  TEST_ASSERT_EQUAL(1, ring.size());
  TEST_ASSERT_EQUAL_STRING("kept", popStr(ring).c_str());
}

// --- peek/dropFront: чтение без удаления (досылка с подтверждением,
// см. EspLogger::flushPending — FIFO не ломается при сбое publish) ---

void test_peek_does_not_remove_line() {
  LogRing ring(256);
  ring.push("first", 5);
  ring.push("second", 6);

  char buf[64];
  // peek возвращает полную длину и не трогает кольцо — повторный peek
  // отдаёт ту же голову (как при неудавшейся публикации)
  TEST_ASSERT_EQUAL(5, ring.peek(buf, sizeof(buf) - 1));
  TEST_ASSERT_EQUAL_STRING_LEN("first", buf, 5);
  TEST_ASSERT_EQUAL(2, ring.size());
  TEST_ASSERT_EQUAL(5, ring.peek(buf, sizeof(buf) - 1));
  TEST_ASSERT_EQUAL_STRING_LEN("first", buf, 5);

  // dropFront(false) — доставлено, потерей не считается
  ring.dropFront(false);
  TEST_ASSERT_EQUAL(1, ring.size());
  TEST_ASSERT_EQUAL_UINT32(0, ring.lostCount());
  TEST_ASSERT_EQUAL(6, ring.peek(buf, sizeof(buf) - 1));
  TEST_ASSERT_EQUAL_STRING_LEN("second", buf, 6);
}

void test_peek_reports_full_length_when_buffer_too_small() {
  LogRing ring(256);
  ring.push("0123456789", 10);

  char buf[8];
  // полная длина больше maxLen — вызывающий узнаёт об этом и может
  // выбросить строку как потерянную, не публикуя обрезанный JSON
  TEST_ASSERT_EQUAL(10, ring.peek(buf, 4));
  TEST_ASSERT_EQUAL_STRING_LEN("0123", buf, 4);

  ring.dropFront(true);  // не доставлена — в счётчик потерь
  TEST_ASSERT_TRUE(ring.empty());
  TEST_ASSERT_EQUAL_UINT32(1, ring.lostCount());
}

void test_peek_empty_ring_returns_zero() {
  LogRing ring(64);
  char buf[8];
  TEST_ASSERT_EQUAL(0, ring.peek(buf, sizeof(buf)));
  ring.dropFront(false);  // на пустом кольце — no-op, не падает
  TEST_ASSERT_TRUE(ring.empty());
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_empty_ring_pop_returns_zero);
  RUN_TEST(test_fifo_order_preserved);
  RUN_TEST(test_overflow_drops_oldest_and_counts_loss);
  RUN_TEST(test_overflow_counts_multiple_losses);
  RUN_TEST(test_lost_count_reset_after_marker);
  RUN_TEST(test_line_larger_than_buffer_is_dropped);
  RUN_TEST(test_peek_does_not_remove_line);
  RUN_TEST(test_peek_reports_full_length_when_buffer_too_small);
  RUN_TEST(test_peek_empty_ring_returns_zero);

  return UNITY_END();
}
