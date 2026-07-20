#include <Arduino.h>
#include <CommandParser.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// --- isValidInteger ---

void test_valid_integer_accepts_digits() {
  TEST_ASSERT_TRUE(isValidInteger(String("0")));
  TEST_ASSERT_TRUE(isValidInteger(String("42")));
  TEST_ASSERT_TRUE(isValidInteger(String("100000")));
}

void test_valid_integer_rejects_garbage() {
  TEST_ASSERT_FALSE(isValidInteger(String("")));
  TEST_ASSERT_FALSE(isValidInteger(String("-5")));
  TEST_ASSERT_FALSE(isValidInteger(String("1a")));
  TEST_ASSERT_FALSE(isValidInteger(String(" 50")));
  TEST_ASSERT_FALSE(isValidInteger(String("5.0")));
}

// --- parsePlantAmountCommand ---

void test_parse_water_command() {
  int id = -1, amount = -1;
  TEST_ASSERT_TRUE(parsePlantAmountCommand(String("/water plant3 50ml"),
                                           String("/water"), id, amount));
  TEST_ASSERT_EQUAL(3, id);
  TEST_ASSERT_EQUAL(50, amount);
}

void test_parse_config_command() {
  int id = -1, amount = -1;
  TEST_ASSERT_TRUE(parsePlantAmountCommand(String("/config plant12 200ml"),
                                           String("/config"), id, amount));
  TEST_ASSERT_EQUAL(12, id);
  TEST_ASSERT_EQUAL(200, amount);
}

// значения до 6 цифр парсятся — границы (id<16, amount<=200) проверяет
// вызывающий код (checkPlantCommandBounds на ESP, isValidPlantCommand на Mega)
void test_parse_large_amount_passes_parser() {
  int id = -1, amount = -1;
  TEST_ASSERT_TRUE(parsePlantAmountCommand(String("/water plant0 100000ml"),
                                           String("/water"), id, amount));
  TEST_ASSERT_EQUAL(0, id);
  TEST_ASSERT_EQUAL(100000, amount);
}

// длиннее 6 цифр — отказ: защита от переполнения toInt/int
// (иначе plant65536 на 16-битном int Mega превращался бы в id=0)
void test_parse_rejects_overlong_numbers() {
  int id, amount;
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant3 1000000ml"),
                                            String("/water"), id, amount));
  TEST_ASSERT_FALSE(parsePlantAmountCommand(
      String("/water plant4294967300 50ml"), String("/water"), id, amount));
}

void test_parse_rejects_missing_ml_suffix() {
  int id, amount;
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant3 50"),
                                            String("/water"), id, amount));
}

void test_parse_rejects_missing_amount() {
  int id, amount;
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant3"),
                                            String("/water"), id, amount));
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant3 ml"),
                                            String("/water"), id, amount));
}

void test_parse_rejects_garbage_id() {
  int id, amount;
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plantXY 50ml"),
                                            String("/water"), id, amount));
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant 50ml"),
                                            String("/water"), id, amount));
}

void test_parse_rejects_wrong_prefix() {
  int id, amount;
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant3 50ml"),
                                            String("/config"), id, amount));
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/waterplant3 50ml"),
                                            String("/water"), id, amount));
}

void test_parse_rejects_double_space() {
  int id, amount;
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant3  50ml"),
                                            String("/water"), id, amount));
}

void test_parse_rejects_negative_amount() {
  int id, amount;
  TEST_ASSERT_FALSE(parsePlantAmountCommand(String("/water plant3 -50ml"),
                                            String("/water"), id, amount));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_valid_integer_accepts_digits);
  RUN_TEST(test_valid_integer_rejects_garbage);

  RUN_TEST(test_parse_water_command);
  RUN_TEST(test_parse_config_command);
  RUN_TEST(test_parse_large_amount_passes_parser);
  RUN_TEST(test_parse_rejects_overlong_numbers);
  RUN_TEST(test_parse_rejects_missing_ml_suffix);
  RUN_TEST(test_parse_rejects_missing_amount);
  RUN_TEST(test_parse_rejects_garbage_id);
  RUN_TEST(test_parse_rejects_wrong_prefix);
  RUN_TEST(test_parse_rejects_double_space);
  RUN_TEST(test_parse_rejects_negative_amount);

  return UNITY_END();
}
