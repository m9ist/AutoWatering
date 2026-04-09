#include <Arduino.h>
#include <Graph.h>
#include <State.h>
#include <unity.h>

// Логгер-заглушка, собирает вывод для проверок в тестах
class CapturingPrint : public Print {
 public:
  std::string output;
  size_t print(const char* s) override {
    if (s) output += s;
    return 0;
  }
  size_t print(int n) override {
    output += std::to_string(n);
    return 0;
  }
  size_t print(unsigned int n) override {
    output += std::to_string(n);
    return 0;
  }
  size_t println(const char* s) override {
    if (s) output += s;
    output += '\n';
    return 0;
  }
  size_t println(int n) override {
    output += std::to_string(n);
    output += '\n';
    return 0;
  }
  size_t println() override {
    output += '\n';
    return 0;
  }
  void clear() { output.clear(); }
};

CapturingPrint logger;

void setUp() { logger.clear(); }
void tearDown() {}

// --- Graph::plot() ---

// Главный баг: одна точка → maxValue==minValue → stepY=0 → деление на ноль
void test_graph_single_point_no_crash() {
  Graph g(1, 1, logger);
  g.addPoint(0, 0, 500);
  String result = g.plot();
  TEST_ASSERT_TRUE(result.length() > 0);
}

// Несколько точек с одинаковым значением — тоже stepY=0
void test_graph_all_same_values_no_crash() {
  Graph g(1, 5, logger);
  for (int i = 0; i < 5; i++) {
    g.addPoint(0, i, 700);
  }
  String result = g.plot();
  TEST_ASSERT_TRUE(result.length() > 0);
}

// Нормальная работа: несколько растений, разные значения
void test_graph_normal_case() {
  Graph g(2, 10, logger);
  for (int i = 0; i < 10; i++) {
    g.addPoint(0, i, 300 + i * 40);
    g.addPoint(5, i, 800 - i * 30);
  }
  String result = g.plot();
  TEST_ASSERT_TRUE(result.length() > 0);
  // в результате должны быть номера растений
  TEST_ASSERT_TRUE(result._s.find("plant number 0") != std::string::npos);
  TEST_ASSERT_TRUE(result._s.find("plant number 5") != std::string::npos);
}

// Нулевые графики — не должен крэшиться, возвращает пустую строку
void test_graph_empty_returns_empty() {
  Graph g(0, 0, logger);
  String result = g.plot();
  TEST_ASSERT_EQUAL(0, result.length());
}

// Переполнение слотов: addPoint для большего числа растений чем numGraphs
// должен логировать ошибку, но не крэшиться
void test_graph_overflow_slots_logs_error() {
  Graph g(1, 3, logger);  // только 1 слот
  g.addPoint(0, 0, 500);
  g.addPoint(1, 0, 600);  // нет слота → должен напечатать предупреждение
  TEST_ASSERT_TRUE(logger.output.find("graphId == -1") != std::string::npos);
  // plot не крэшится
  g.plot();
}

// --- PointsHoler ---

// Фикс PLANTS_AMOUNT→MEDIAN_NUM_POINTS: высокий plant_id не выходит за границы
void test_pointsholder_high_plant_id_no_crash() {
  PointsHoler ph(logger);
  // добавляем 3 значения для последнего растения (id=15)
  ph.addPoint(15, 700);
  ph.addPoint(15, 710);
  ph.addPoint(15, 720);

  bool called = false;
  ph.dumpAvg([&](uint8_t plant, uint16_t avg) {
    if (plant == 15) {
      called = true;
      TEST_ASSERT_GREATER_OR_EQUAL(700, avg);
      TEST_ASSERT_LESS_OR_EQUAL(720, avg);
    }
  });
  TEST_ASSERT_TRUE(called);
}

// Медиана для plant 0 (sort работает корректно при start=0)
void test_pointsholder_median_plant0() {
  PointsHoler ph(logger);
  // значения: 300 700 100 400 200 600 500
  // отсортированные: 100 200 300 400 500 600 700
  // индекс медианы = 0 + 7/2 + 7%2 = 4 → значение 500
  uint16_t vals[] = {300, 700, 100, 400, 200, 600, 500};
  for (int i = 0; i < 7; i++) ph.addPoint(0, vals[i]);

  uint16_t got = 0;
  ph.dumpAvg([&](uint8_t plant, uint16_t avg) {
    if (plant == 0) got = avg;
  });
  TEST_ASSERT_EQUAL(500, got);
}

// dumpAvg с неполным буфером (меньше MEDIAN_NUM_POINTS точек) — берёт первый элемент
void test_pointsholder_partial_buffer() {
  PointsHoler ph(logger);
  ph.addPoint(0, 400);
  ph.addPoint(0, 600);

  uint16_t got = 0;
  bool called = false;
  ph.dumpAvg([&](uint8_t plant, uint16_t avg) {
    if (plant == 0) {
      called = true;
      got = avg;
    }
  });
  TEST_ASSERT_TRUE(called);
  // _ids[0]=2 < 3 → возвращает _graphs[0] = 400
  TEST_ASSERT_EQUAL(400, got);
}

// dumpAvg без данных — callback не вызывается
void test_pointsholder_empty_no_callback() {
  PointsHoler ph(logger);
  int count = 0;
  ph.dumpAvg([&](uint8_t, uint16_t) { count++; });
  TEST_ASSERT_EQUAL(0, count);
}

// dumpAvg очищает состояние после вызова
void test_pointsholder_clears_after_dump() {
  PointsHoler ph(logger);
  ph.addPoint(0, 500);

  int count = 0;
  ph.dumpAvg([&](uint8_t, uint16_t) { count++; });
  TEST_ASSERT_EQUAL(1, count);

  // после дампа — пустой
  count = 0;
  ph.dumpAvg([&](uint8_t, uint16_t) { count++; });
  TEST_ASSERT_EQUAL(0, count);
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_graph_single_point_no_crash);
  RUN_TEST(test_graph_all_same_values_no_crash);
  RUN_TEST(test_graph_normal_case);
  RUN_TEST(test_graph_empty_returns_empty);
  RUN_TEST(test_graph_overflow_slots_logs_error);

  RUN_TEST(test_pointsholder_high_plant_id_no_crash);
  RUN_TEST(test_pointsholder_median_plant0);
  RUN_TEST(test_pointsholder_partial_buffer);
  RUN_TEST(test_pointsholder_empty_no_callback);
  RUN_TEST(test_pointsholder_clears_after_dump);

  return UNITY_END();
}
