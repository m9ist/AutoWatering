#include <CmdHandler.h>
#include <unity.h>

#include <cstring>

// PLANTS_AMOUNT/MAX_WATER_AMOUNT_ML как в src/State.h (16 растений, 200мл) —
// см. комментарий в CmdHandler.h, почему модуль не тянет реальный State.h.
static const int kPlantsAmount = 16;
static const int kMaxWaterAmountMl = 200;
// MAX_WAKEUP_CYCLES/MAX_WAKEUP_ON_MS как в src/State.h
static const int kMaxWakeupCycles = 30;
static const int kMaxWakeupOnMs = 5000;

using cmdhandler::Action;
using cmdhandler::decideCmd;
using cmdhandler::Decision;
using cmdhandler::isPlantCommandInBounds;
using cmdhandler::isWakeupCommandInBounds;

static Decision decide(const char* json) {
  return decideCmd(json, strlen(json));
}

// Полный пайплайн, как в EspMain.cpp::handleCmdMessage: decideCmd +
// checkPlantCommandBounds (тут — чистая isPlantCommandInBounds).
static bool isRejectedEndToEnd(const char* json) {
  Decision d = decide(json);
  if (d.action == Action::kReject) return true;
  if (d.action == Action::kWater || d.action == Action::kConfig) {
    return !isPlantCommandInBounds(d.plantId, d.amountMl, kPlantsAmount,
                                   kMaxWaterAmountMl);
  }
  if (d.action == Action::kWakeup) {
    return !isWakeupCommandInBounds(d.plantId, d.longCycles, d.longOnMs,
                                    d.shortCycles, d.shortOnMs, kPlantsAmount,
                                    kMaxWakeupCycles, kMaxWakeupOnMs);
  }
  return false;
}

void setUp() {}
void tearDown() {}

// --- валидные команды: правильные поля ---

void test_valid_water_command() {
  Decision d = decide("{\"c\":\"esp_water\",\"plantId\":3,\"amountMl\":50}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kWater), static_cast<int>(d.action));
  TEST_ASSERT_EQUAL(3, d.plantId);
  TEST_ASSERT_EQUAL(50, d.amountMl);
  TEST_ASSERT_TRUE(isPlantCommandInBounds(d.plantId, d.amountMl, kPlantsAmount,
                                          kMaxWaterAmountMl));
}

void test_valid_config_command() {
  Decision d = decide("{\"c\":\"esp_plant_conf\",\"plantId\":12,\"amountMl\":20}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kConfig), static_cast<int>(d.action));
  TEST_ASSERT_EQUAL(12, d.plantId);
  TEST_ASSERT_EQUAL(20, d.amountMl);
  TEST_ASSERT_TRUE(isPlantCommandInBounds(d.plantId, d.amountMl, kPlantsAmount,
                                          kMaxWaterAmountMl));
}

// --- команды без plantId/amountMl: passthrough-форвард ---

void test_daily_command_recognized() {
  Decision d = decide("{\"c\":\"esp_daily\"}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kDaily), static_cast<int>(d.action));
}

void test_check_valves_command_recognized() {
  Decision d = decide("{\"c\":\"esp_check_valves\"}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kCheckValves), static_cast<int>(d.action));
}

void test_switch_pump_command_recognized() {
  Decision d = decide("{\"c\":\"esp_pump\"}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kSwitchPump), static_cast<int>(d.action));
}

// --- esp_graphs больше не существует (issue #20) — отвергается ---

void test_graphs_command_is_rejected() {
  Decision d = decide("{\"c\":\"esp_graphs\"}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kReject), static_cast<int>(d.action));
}

// --- isPlantCommandInBounds: границы включительно ---

void test_bounds_accepts_edge_values() {
  // id=15 (PLANTS_AMOUNT-1), amount=200 (MAX_WATER_AMOUNT_ML) — валидны
  TEST_ASSERT_TRUE(isPlantCommandInBounds(15, 200, kPlantsAmount, kMaxWaterAmountMl));
  TEST_ASSERT_TRUE(isPlantCommandInBounds(0, 0, kPlantsAmount, kMaxWaterAmountMl));
}

void test_bounds_rejects_plant_id_out_of_range() {
  TEST_ASSERT_FALSE(isPlantCommandInBounds(16, 50, kPlantsAmount, kMaxWaterAmountMl));
  TEST_ASSERT_FALSE(isPlantCommandInBounds(99, 50, kPlantsAmount, kMaxWaterAmountMl));
  TEST_ASSERT_FALSE(isPlantCommandInBounds(-1, 50, kPlantsAmount, kMaxWaterAmountMl));
}

void test_bounds_rejects_amount_over_limit() {
  TEST_ASSERT_FALSE(isPlantCommandInBounds(3, 201, kPlantsAmount, kMaxWaterAmountMl));
  TEST_ASSERT_FALSE(isPlantCommandInBounds(3, -5, kPlantsAmount, kMaxWaterAmountMl));
}

// --- отказы end-to-end (decideCmd + isPlantCommandInBounds), как в EspMain.cpp ---

void test_plant99_rejected_end_to_end() {
  TEST_ASSERT_TRUE(
      isRejectedEndToEnd("{\"c\":\"esp_water\",\"plantId\":99,\"amountMl\":50}"));
}

void test_amount_over_limit_rejected_end_to_end() {
  TEST_ASSERT_TRUE(
      isRejectedEndToEnd("{\"c\":\"esp_water\",\"plantId\":3,\"amountMl\":9999}"));
}

void test_water_command_missing_fields_rejected_end_to_end() {
  // plantId/amountMl отсутствуют -> decideCmd даёт -1/-1 -> вне границ
  TEST_ASSERT_TRUE(isRejectedEndToEnd("{\"c\":\"esp_water\"}"));
}

// --- отказы: битый JSON ---

void test_malformed_json_rejected() {
  Decision d = decide("{\"c\":\"esp_water\", oops");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kReject), static_cast<int>(d.action));
  TEST_ASSERT_TRUE(strlen(d.reason) > 0);
  TEST_ASSERT_TRUE(isRejectedEndToEnd("{\"c\":\"esp_water\", oops"));
}

void test_empty_payload_rejected() {
  Decision d = decide("");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kReject), static_cast<int>(d.action));
}

// --- отказы: неизвестная команда / отсутствие "c" ---

void test_unknown_command_rejected() {
  Decision d = decide("{\"c\":\"esp_unknown\"}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kReject), static_cast<int>(d.action));
  TEST_ASSERT_TRUE(strlen(d.reason) > 0);
  TEST_ASSERT_TRUE(isRejectedEndToEnd("{\"c\":\"esp_unknown\"}"));
}

void test_missing_command_key_rejected() {
  Decision d = decide("{\"plantId\":3,\"amountMl\":50}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kReject), static_cast<int>(d.action));
}

// --- оживление клапана (esp_wakeup) ---

void test_valid_wakeup_command() {
  Decision d = decide("{\"c\":\"esp_wakeup\",\"plantId\":3,\"longCycles\":3,\"longOnMs\":2000,\"shortCycles\":7,\"shortOnMs\":300}");
  TEST_ASSERT_EQUAL(static_cast<int>(Action::kWakeup), static_cast<int>(d.action));
  TEST_ASSERT_EQUAL(3, d.plantId);
  TEST_ASSERT_EQUAL(3, d.longCycles);
  TEST_ASSERT_EQUAL(2000, d.longOnMs);
  TEST_ASSERT_EQUAL(7, d.shortCycles);
  TEST_ASSERT_EQUAL(300, d.shortOnMs);
  TEST_ASSERT_TRUE(isWakeupCommandInBounds(d.plantId, d.longCycles, d.longOnMs,
                                           d.shortCycles, d.shortOnMs,
                                           kPlantsAmount, kMaxWakeupCycles,
                                           kMaxWakeupOnMs));
}

// Поля отсутствуют -> -1 -> отказ на границах, а не молчаливый прогон
// с мусорной формулой.
void test_wakeup_command_missing_fields_rejected_end_to_end() {
  TEST_ASSERT_TRUE(isRejectedEndToEnd("{\"c\":\"esp_wakeup\"}"));
  TEST_ASSERT_TRUE(isRejectedEndToEnd(
      "{\"c\":\"esp_wakeup\",\"plantId\":3}"));
}

void test_wakeup_bounds_accept_edge_values() {
  TEST_ASSERT_TRUE(isWakeupCommandInBounds(0, 0, 0, 0, 0, kPlantsAmount,
                                           kMaxWakeupCycles, kMaxWakeupOnMs));
  TEST_ASSERT_TRUE(isWakeupCommandInBounds(15, 30, 5000, 30, 5000,
                                           kPlantsAmount, kMaxWakeupCycles,
                                           kMaxWakeupOnMs));
}

void test_wakeup_bounds_reject_out_of_range() {
  // id вне массива растений
  TEST_ASSERT_FALSE(isWakeupCommandInBounds(16, 3, 2000, 7, 300, kPlantsAmount,
                                            kMaxWakeupCycles, kMaxWakeupOnMs));
  // опечатка в числе циклов
  TEST_ASSERT_FALSE(isWakeupCommandInBounds(0, 31, 2000, 7, 300, kPlantsAmount,
                                            kMaxWakeupCycles, kMaxWakeupOnMs));
  // опечатка в длительности: 3x20000 держал бы клапан 20 секунд подряд
  TEST_ASSERT_FALSE(isWakeupCommandInBounds(0, 3, 20000, 7, 300, kPlantsAmount,
                                            kMaxWakeupCycles, kMaxWakeupOnMs));
  TEST_ASSERT_FALSE(isWakeupCommandInBounds(0, 3, 2000, 7, 9000, kPlantsAmount,
                                            kMaxWakeupCycles, kMaxWakeupOnMs));
  TEST_ASSERT_FALSE(isWakeupCommandInBounds(-1, -1, -1, -1, -1, kPlantsAmount,
                                            kMaxWakeupCycles, kMaxWakeupOnMs));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_valid_water_command);
  RUN_TEST(test_valid_config_command);

  RUN_TEST(test_daily_command_recognized);
  RUN_TEST(test_check_valves_command_recognized);
  RUN_TEST(test_switch_pump_command_recognized);
  RUN_TEST(test_valid_wakeup_command);
  RUN_TEST(test_graphs_command_is_rejected);

  RUN_TEST(test_bounds_accepts_edge_values);
  RUN_TEST(test_bounds_rejects_plant_id_out_of_range);
  RUN_TEST(test_bounds_rejects_amount_over_limit);

  RUN_TEST(test_plant99_rejected_end_to_end);
  RUN_TEST(test_amount_over_limit_rejected_end_to_end);
  RUN_TEST(test_water_command_missing_fields_rejected_end_to_end);
  RUN_TEST(test_wakeup_command_missing_fields_rejected_end_to_end);
  RUN_TEST(test_wakeup_bounds_accept_edge_values);
  RUN_TEST(test_wakeup_bounds_reject_out_of_range);

  RUN_TEST(test_malformed_json_rejected);
  RUN_TEST(test_empty_payload_rejected);

  RUN_TEST(test_unknown_command_rejected);
  RUN_TEST(test_missing_command_key_rejected);

  return UNITY_END();
}
