#ifndef ESP_LOGGER_H
#define ESP_LOGGER_H
#include <Arduino.h>

// Минимальный логгер за прежним интерфейсом (наследует Print, как раньше
// sets::Logger из SettingsGyver) — им пользуются Communication.h и
// PointsHoler/Graph (Graph.h). Пока сток — заглушка, write() никуда не
// пишет: MQTT-сток (публикация в aw/log/esp) добавит следующий тикет (#16).
class EspLogger : public Print {
 public:
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t* buffer, size_t size) override { return size; }
};

#endif  // ESP_LOGGER_H
