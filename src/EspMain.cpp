#define NUM_PLOT_POINTS 48
// #define WITHOUT_ARDUINO

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <CommandParser.h>
#include <Communication.h>
#include <ESP8266WiFi.h>
#include <EspLogger.h>
#include <Graph.h>
#include <State.h>
#include <Timer.h>
#include <time.h>

// Блок из SecretHolder, чтобы не заводить лишний .h файл, нужно там реализовать
// эти методы
String ssid();
String wifiPasswork();

EspLogger logger;

// точки для графиков собираются по своему таймеру (раньше были привязаны к
// часовой отправке стейта в телеграм — этот канал снят, см. issue #13)
Timer timerGatherPoint;
const Duration repeatIntervalGatherPoint = Timer::Hours(1);
bool stateReceived = false;

// полученный из ардуино стейт
State lastState;

struct PlotPoint {
  uint32_t time;
  uint16_t pp[PLANTS_AMOUNT];
};
int numPlotPoints = 0;
int numPlants = 0;
PlotPoint lastPlotPoints[NUM_PLOT_POINTS];
PointsHoler pointsHolder = PointsHoler(logger);

Communication comm = Communication(Serial, logger, true);

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void serialLog(const String& command, const String& s) {
#ifdef DEBUG_LOG
  JsonDocument json;
  json[COMMAND_KEY] = command;
  json[F("timestamp")] = getTimestamp();
  json[F("log")] = s;

  String sendJson;
  serializeJson(json, sendJson);
#ifdef WITHOUT_ARDUINO
  Serial.println(sendJson);
#else
  logger.println(sendJson);
  comm.communicationSendMessage(sendJson);
#endif
#endif
}

void serialLog(const String& s) { serialLog(ESP_COMMAND_LOG, s); }

// Отправка в ардуино команды с растением и объёмом (esp_water / esp_plant_conf)
void serialPlantCommand(const String& command, int id, int amount) {
  JsonDocument json;
  json[COMMAND_KEY] = command;
  json[F("timestamp")] = getTimestamp();
  json[F("plantId")] = id;
  json[F("amountMl")] = amount;

  String sendJson;
  serializeJson(json, sendJson);
  logger.println(sendJson);
  comm.communicationSendMessage(sendJson);
}

void serialTimeSynced() {
  JsonDocument json;
  json[COMMAND_KEY] = ESP_COMMAND_TIME_SYNCED;
  json[F("timestamp")] = getTimestamp();
  time_t now = time(nullptr);
  struct tm timeinfo;
  // localtime, а не gmtime: в RTC ардуины и на экран должно уехать местное
  localtime_r(&now, &timeinfo);
  serializeTimeInfo(timeinfo, json);
  String sendJson;
  serializeJson(json, sendJson);
  logger.println(sendJson);
  comm.communicationSendMessage(sendJson);
}

void serialError(const String& s) { serialLog(ESP_COMMAND_LOG, s); }

// Границы значений проверяем ещё на ESP: на Mega int 16-битный, без этой
// проверки plant65536 усекался бы до валидного id. Mega проверяет тоже
// (защита своего протокола), но с менее внятным сообщением пользователю.
// Сейчас не вызывается (телеграм-канал снят, см. issue #13) — понадобится
// для валидации команд из aw/cmd (MQTT), см. issue #17.
bool checkPlantCommandBounds(int id, int amount) {
  if (id >= 0 && id < PLANTS_AMOUNT && amount >= 0 &&
      amount <= MAX_WATER_AMOUNT_ML) {
    return true;
  }
  serialLog((String)F("Rejected: plant id must be 0..") +
            (PLANTS_AMOUNT - 1) + F(", amount 0..") + MAX_WATER_AMOUNT_ML +
            F("ml"));
  return false;
}

bool isTimeInited() { return time(nullptr) > 1000000000; }

void checkWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {  // Ожидаем подключения к Wi-Fi
    serialLog(F("Waiting wi-fi"));
    delay(1000);
    i++;
    if (i > 15) {
      serialLog(F("Unable to connect wifi, restart."));
      ESP.restart();
    }
  }

  // Выводим информацию о подключении
  serialLog(F("Connected to ") + ssid());
  serialLog(WiFi.localIP().toString());
}

void setupOTA() {
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = F("sketch");
    } else {  // U_FS
      type = F("filesystem");
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    serialLog(F("Start updating ") + type);
  });
  ArduinoOTA.onEnd([]() { serialLog(F("End")); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println(F("Auth Failed"));
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println(F("Begin Failed"));
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println(F("Connect Failed"));
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println(F("Receive Failed"));
    } else if (error == OTA_END_ERROR) {
      Serial.println(F("End Failed"));
    }
  });
  ArduinoOTA.begin();
}

void setup() {
#ifdef WITHOUT_ARDUINO
  Serial.begin(9600);  // для отладки
  delay(3000);
#else
  Serial.begin(115200);  // для общения с ардуино
#endif
  while (!Serial) {
  }

  serialLog("Start working");

  // Устанавливаем Wi-Fi модуль в режим клиента (STA)
  WiFi.mode(WIFI_STA);
  // Устанавливаем ssid и пароль от сети, подключаемся
  WiFi.begin(ssid(), wifiPasswork());
  checkWifi();
  setupOTA();

  serialLog(F("Start sync time"));
  // POSIX TZ: знак инвертирован, московское UTC+3 записывается как MSK-3
  configTime("MSK-3", F("ntp6.ntp-servers.net"), F("1.ru.pool.ntp.org"),
             F("ntp.ix.ru"));
  while (!isTimeInited()) {
    serialLog(F("Waiting for time sync..."));
    delay(1000);
  }
  serialTimeSynced();

  timerGatherPoint.setDuration(repeatIntervalGatherPoint);
  serialLog(F("Esp started successfully."));
}

void gatherPoint() {
  PlotPoint point;
  time_t now = time(nullptr);
  point.time = now;
  int lNumPlants = 0;
  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    point.pp[i] = UNDEFINED_PLANT_VALUE;
  }
  String info;
  pointsHolder.dumpAvg([&](uint8_t plant, uint16_t avg) {
    point.pp[plant] = avg;
    lNumPlants++;
    logger.print(F(", "));
    logger.print(plant);
    logger.print(F(" = "));
    logger.print(avg);
  });
  if (lNumPlants > numPlants) {
    numPlants = lNumPlants;
  }
  logger.print(F("Got point: "));
  for (int i = 0; i < PLANTS_AMOUNT; i++) {
    logger.print(point.pp[i]);
    logger.print(',');
  }
  logger.println(';');
  if (numPlotPoints == NUM_PLOT_POINTS) {
    for (int i = 0; i < NUM_PLOT_POINTS - 1; i++) {
      lastPlotPoints[i] = lastPlotPoints[i + 1];
    }
    numPlotPoints--;
  }
  lastPlotPoints[numPlotPoints] = point;
  numPlotPoints++;
}

void processMessageArduino(String message) {
  logger.println(getTimestamp());
  logger.println(message);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error != DeserializationError::Ok) {
    serialError((String)F("Can't deserialize ") + error.c_str());
    serialLog(message);
  } else {
    const char* command = doc[COMMAND_KEY];
    if ((String)ARDUINO_COMMAND_STATE == command) {
      State state = deserializeState(doc);
      lastState = state;
      stateReceived = true;

      for (int i = 0; i < PLANTS_AMOUNT; i++) {
        if (isDefined(lastState.plants[i])) {
          pointsHolder.addPoint(i, lastState.plants[i].originalValue);
        }
      }
    } else if ((String)ARDUINO_SEND_TELEGRAM == command) {
      // раньше пересылалось в телеграм (logTelegram) — канал снят (#13),
      // просто логируем; событие для человека появится через aw/event
      // после MQTT-слоя (#16/#17)
      const char* message = doc[F("message")];
      serialLog((String)F("Arduino message (telegram channel removed): ") +
                message);
    } else {
      serialError((String)F("Unknown command ") + command);
      serialLog(message);
    }
  }
}

void loop() {
  ArduinoOTA.handle();
  // todo вынести в другой поток? <<<<<<<<<<<<<<<<<<<<<<
  comm.communicationTick();
  if (comm.communicationHasMessage()) {
    processMessageArduino(comm.communicationGetMessage());
    return;
  }

  if (timerGatherPoint.expired() && stateReceived) {
    stateReceived = false;

    timerGatherPoint.setDuration(repeatIntervalGatherPoint);
    serialLog(F("Gather points for graph"));
    gatherPoint();
    return;
  }
}