// 3 дня раз для замеров раз в repeatIntervalStateSendIot
#define NUM_PLOT_POINTS 48
#define TURN_ON_TELEGRAM
#define TURN_ON_GYVER
// #define WITHOUT_ARDUINO

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Communication.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <Graph.h>
#ifdef TURN_ON_GYVER
#include <SettingsGyver.h>
#else
#include <DEVFULL.h>
#endif
#include <State.h>
#include <Timer.h>
#ifdef TURN_ON_TELEGRAM
#include <UniversalTelegramBot.h>
#endif
#include <WiFiClientSecure.h>
#include <time.h>

// Блок из SecretHolder, чтобы не заводить лишний .h файл, нужно там реализовать
// эти методы
String ssid();
String wifiPasswork();
String publicCert();
String privateCert();
String getDeviceId();
String getTelegramBotToken();

// Настройки Яндекс IoT
const char* yandex_iot_endpoint = "iot-data.api.cloud.yandex.net";
const int yandex_iot_port = 443;

WiFiClientSecure espClient;
String secretKey;
#ifdef TURN_ON_TELEGRAM
UniversalTelegramBot telegramBot(getTelegramBotToken(), espClient);
// todo <<<<<<<<<<<<<<< по хорошему надо запоминать между запусками точку
// общения, либо вообще на несколько точек завязаться
String telegramChatId = "-5065686553";
Timer timerTelegramCheck;
const Duration repeatTelegramCheck = Timer::Seconds(10);

#define GRAPH_HEIGHT 10
#define GRAPH_LENGHT_MAX 40
#define GRAPH_LABELS 5
#endif

#ifdef TURN_ON_GYVER
SettingsGyver sett("Auto watering");
sets::Logger logger(2500);
bool updatedSettings = false;
#else
DEVFULL logger;
#endif

Timer timerStateSendIot;
const Duration repeatIntervalStateSendIot = Timer::Minutes(30);
bool stateRecievedIot = false;
Timer timerStateSendTelegram;
const Duration repeatIntervalStateSendTelegram = Timer::Hours(1);
bool stateRecievedTelegram = false;

// полученный из ардуино стейт
State lastState;

struct PlotPoint {
  uint32_t time;
  uint16_t pp[PLANTS_AMOUNT];
};
int numPlotPoints = 0;
int numPlants = 0;
PlotPoint lastPlotPoints[NUM_PLOT_POINTS];

Communication comm = Communication(Serial, logger, true);

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%SZ", &timeinfo);
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

void serialCommandWater(int id, int amount) {
  JsonDocument json;
  json[COMMAND_KEY] = ESP_COMMAND_WATER_PLANT;
  json[F("timestamp")] = getTimestamp();
  json[F("plantId")] = id;
  json[F("amountMl")] = amount;

  String sendJson;
  serializeJson(json, sendJson);
  logger.println(sendJson);
  comm.communicationSendMessage(sendJson);
}

void serialConfig(int id, int amount) {
  JsonDocument json;
  json[COMMAND_KEY] = ESP_COMMAND_CONFIG_PLANT;
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
  gmtime_r(&now, &timeinfo);
  serializeTimeInfo(timeinfo, json);
  String sendJson;
  serializeJson(json, sendJson);
  logger.println(sendJson);
  comm.communicationSendMessage(sendJson);
}

void serialError(const String& s) { serialLog(ESP_COMMAND_LOG, s); }

void logTelegram(String message) {
  if (telegramChatId == "") return;

#ifdef TURN_ON_TELEGRAM
  if (message.length() < 100) {
    serialLog((String)F("Send telegram: ") + message);
  }
  telegramBot.sendMessage(telegramChatId, getTimestamp() + F(": ") + message, F("Markdown"), 0);
#endif
}

#ifdef TURN_ON_GYVER
void build(sets::Builder& b) {
  b.Log(logger, "State");
  b.PlotStack(H(run), "p0;p1;p2;p3;p4;p5;p6;p7;p8;p9;p10;p11;p12;p13;p14;p15");
  if (PLANTS_AMOUNT != 16) serialError("Plants amount not 16!");
  if (b.beginButtons()) {
    if (b.Button("Confirm")) {
      updatedSettings = true;
    }
    b.endButtons();
  }
}
#endif

String getBearerAuthKey() {
  // если пустой или прошел час, то обновляем
  return secretKey;
}

void setupWifiClient() {
  // Настройка безопасного соединения
  serialLog(F("Setup secure wifi..."));
  // BearSSL::X509List xYa(ya_sert);
  // espClient.setTrustAnchors(&xYa);
  espClient.setTimeout(15000);
  espClient.setBufferSizes(4096, 4096);
  espClient.setInsecure();
  BearSSL::X509List x509(publicCert().c_str());
  BearSSL::PrivateKey* privKey = new BearSSL::PrivateKey(privateCert().c_str());
  espClient.setClientRSACert(&x509, privKey);

  serialLog(F("Check connection..."));
  if (!espClient.connect(yandex_iot_endpoint, yandex_iot_port)) {
    serialLog((String)F("Connection to iot failed. error = ") +
              espClient.getLastSSLError());
  } else {
    serialLog(F("Connection to iot is tested."));
  }
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

#ifdef TURN_ON_TELEGRAM
bool isValidInteger(String str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}

String getGrapghString() {
  Graph graph = Graph(numPlants, numPlotPoints);
  for (int i = 0; i < numPlotPoints; i++) {
    for (int p = 0; p < PLANTS_AMOUNT; p++) {
      if (lastPlotPoints[i].pp[p] < UNDEFINED_PLANT_VALUE) {
        graph.addPoint(p, i, lastPlotPoints[i].pp[p]);
      }
    }
  }
  return graph.plot();
}

void procesTelegramMessage(String message) {
  if (message == F("/state")) {
    timerStateSendTelegram.setDuration(Timer::Seconds(1));
    logTelegram(F("Setup timer to 1sec to force process state"));
    return;
  }

  if (message == F("/help")) {
    logTelegram(F(
        "\nIf you want to water plant use command /water plantX Yml, where X "
        "in 0..15\n\n  Example: /water plant3 50ml\n\n\n"
        "If you want to config water amount use command /config plantX Yml\n\n"
        "  Example: /config plant2 20ml"));
    return;
  }

  if (message == F("/graphs")) {
    serialLog(ESP_COMMAND_LOG, F("Got graph command"));
    String g = getGrapghString();
    logTelegram(g);
    return;
  }

  if (message == F("/daily")) {
    serialLog(ESP_COMMAND_DAILY_TASK, F("From telegram"));
    return;
  }

  if (message.startsWith(F("/water"))) {
    int mlPos = message.indexOf(" ", 8);
    if (mlPos < 0) {
      logTelegram(F("Invalid command format"));
      return;
    }
    String plantId = message.substring(12, mlPos);
    String amount = message.substring(mlPos + 1, message.length() - 2);
    if (!isValidInteger(plantId) || !isValidInteger(amount)) {
      logTelegram(F("Invalid command format"));
      return;
    }
    serialLog((String)F("Got command from telegram to water plant id=") +
              plantId + F(", amount=") + amount + F("ml."));

    serialCommandWater(plantId.toInt(), amount.toInt());
    return;
  }

  if (message.startsWith(F("/config"))) {
    int mlPos = message.indexOf(" ", 9);
    if (mlPos < 0) {
      logTelegram(F("Invalid command format"));
      return;
    }
    String plantId = message.substring(13, mlPos);
    String amount = message.substring(mlPos + 1, message.length() - 2);
    if (!isValidInteger(plantId) || !isValidInteger(amount)) {
      logTelegram(F("Invalid command format"));
      return;
    }
    serialLog((String)F("Got command from telegram to water plant id=") +
              plantId + F(", amount=") + amount + F("ml."));

    serialConfig(plantId.toInt(), amount.toInt());
    return;
  }
}
#endif

void loopTelegram() {
  if (!timerTelegramCheck.expired()) return;
  timerTelegramCheck.setDuration(repeatTelegramCheck);

#ifdef TURN_ON_TELEGRAM
  int numNewMessages =
      telegramBot.getUpdates(telegramBot.last_message_received + 1);
  if (numNewMessages == 0) return;
  serialLog(F("Telegram start communication!"));
  logTelegram(F("Got it"));
  for (int i = 0; i < numNewMessages; i++) {
    String chatId = telegramBot.messages[i].chat_id;
    if (telegramChatId != chatId) {
      serialLog(F("Got new chatid ") + chatId);
      telegramChatId = chatId;
    }
    String message = telegramBot.messages[i].text;
    message.trim();
    serialLog((String)F("Got message from telegram: ") + message);
    if (message.endsWith(F("@AquatoriaAutoWatering_bot"))) {
      message = message.substring(0, message.length() - 26);
      serialLog((String)F("Updated message: ") + message);
    }
    procesTelegramMessage(message);
  }
#endif
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
  configTime("UTC+3", F("ntp6.ntp-servers.net	"), F("1.ru.pool.ntp.org"),
             F("ntp.ix.ru"));
  while (!isTimeInited()) {
    serialLog(F("Waiting for time sync..."));
    delay(1000);
  }
  serialTimeSynced();

  setupWifiClient();

#ifdef TURN_ON_GYVER
  sett.begin();
  sett.onBuild(build);
  // sett.onUpdate(updateSettings);
#endif

#ifdef TURN_ON_TELEGRAM
  // telegramBot.longPoll = 20;
#endif

  timerStateSendIot.setDuration(repeatIntervalStateSendIot);
  timerStateSendTelegram.setDuration(repeatIntervalStateSendTelegram);
  timerTelegramCheck.setDuration(repeatTelegramCheck);
  logTelegram(F("Esp started successfully."));
}

void sendMessageToIOT() {
  serialLog(F("Start sending message to iot..."));
  if (true) {
    serialLog(F("Communication with iot is turned off, exit."));
    return;
  }
  HTTPClient https;
  String url = F("https://");
  url += yandex_iot_endpoint;
  url += F("/iot-devices/v1/devices/");
  url += getDeviceId();
  url += F("/publish");
  https.begin(espClient, url);

  https.addHeader(F("Content-Type"), F("application/json"));
  https.addHeader(F("Connection"), F("close"));
  https.addHeader(F("Authorization"),
                  (String)F("Bearer ") + getBearerAuthKey());

  JsonDocument subDoc;
  subDoc[F("timestamp")] = getTimestamp();
  subDoc[F("ip_address")] = WiFi.localIP().toString();
  subDoc[F("rssi")] = WiFi.RSSI();

  JsonDocument doc;
  doc[F("topic")] = "events";
  // doc["data"] = 1;
  doc[F("data")] = "test";

  String json;
  serializeJson(doc, json);

  serialLog(F("post: ") + json);
  int httpCode = https.POST(json);
  serialLog((String)F("got result ") + httpCode);
  String response = https.getString();
  serialLog(F("Response: ") + response);
  https.end();
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
      stateRecievedIot = true;
      stateRecievedTelegram = true;

      // подрисуем сразу точки на графике
      PlotPoint point;
      time_t now = time(nullptr);
      point.time = now;
      int lNumPlants = 0;
      for (int i = 0; i < PLANTS_AMOUNT; i++) {
        if (isDefined(lastState.plants[i])) {
          point.pp[i] = lastState.plants[i].originalValue;
          lNumPlants++;
        } else {
          point.pp[i] = UNDEFINED_PLANT_VALUE;
        }
      }
      if (lNumPlants > numPlants) {
        numPlants = lNumPlants;
      }
      if (numPlotPoints == NUM_PLOT_POINTS) {
        for (int i = 0; i < NUM_PLOT_POINTS - 1; i++) {
          lastPlotPoints[i] = lastPlotPoints[i + 1];
        }
        numPlotPoints--;
      }
      lastPlotPoints[numPlotPoints] = point;
      numPlotPoints++;
    } else if ((String)ARDUINO_SEND_TELEGRAM == command) {
      const char* message = doc[F("message")];
      logTelegram(message);
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

#ifdef TURN_ON_GYVER
  sett.tick();
#endif
  loopTelegram();

  if (timerStateSendIot.expired() && stateRecievedIot) {
    stateRecievedIot = false;

    timerStateSendIot.setDuration(repeatIntervalStateSendIot);
    sendMessageToIOT();
    return;
  }

  if (timerStateSendTelegram.expired() && stateRecievedTelegram) {
    stateRecievedTelegram = false;
    serialLog(F("Send state to telegram"));

    timerStateSendTelegram.setDuration(repeatIntervalStateSendTelegram);
    JsonDocument des = serializeState(lastState);
    String outJson;
    serializeJson(des, outJson);
    logTelegram((String)F("State: ") + outJson);
    return;
  }
}