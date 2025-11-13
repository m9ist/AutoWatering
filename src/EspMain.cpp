#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SettingsGyver.h>
#include <State.h>
#include <Timer.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Communication.h>

// Блок из SecretHolder, чтобы не заводить лишний .h файл, нужно там реализовать
// эти методы
String ssid();
String wifiPasswork();
String publicCert();
String privateCert();
String getDeviceId();

// Настройки Яндекс IoT
const char* yandex_iot_endpoint = "iot-data.api.cloud.yandex.net";
const int yandex_iot_port = 443;

WiFiClientSecure espClient;
String secretKey;

SettingsGyver sett("Auto watering");
sets::Logger logger(2500);
bool updatedSettings = false;

Timer timerStateSend;
const Duration repeatIntervalStateSend = Timer::Minutes(30);
bool stateRecieved = false;
State lastState;

Communication comm = Communication(Serial, logger, true);

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

void sendSerial(String message) {
  // todo inline
  logger.println(message);
  comm.communicationSendMessage(message);
}

void serialLog(const String& command, const String& s) {
  JsonDocument json;
  json[command_key] = command;
  json["timestamp"] = getTimestamp();
  json["log"] = s;

  String sendJson;
  serializeJson(json, sendJson);
  sendSerial(sendJson);
}

void serialLog(const String& s) { serialLog(esp_command_log, s); }

void serialTimeSynced() {
  JsonDocument json;
  json[command_key] = esp_command_time_synced;
  json["timestamp"] = getTimestamp();
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  serializeTimeInfo(timeinfo, json);
  String sendJson;
  serializeJson(json, sendJson);

  sendSerial(sendJson);
}

void serialError(const String& s) { serialLog(esp_command_log, s); }

void build(sets::Builder& b) {
  b.Log(logger, "State");
  if (b.beginButtons()) {
    if (b.Button("Confirm")) {
      updatedSettings = true;
    }
    b.endButtons();
  }
}

void updateSettings(sets::Updater& u) {}

String getBearerAuthKey() {
  // если пустой или прошел час, то обновляем
  return secretKey;
}

void setupWifiClient() {
  // Настройка безопасного соединения
  serialLog("Setup secure wifi...");
  // BearSSL::X509List xYa(ya_sert);
  // espClient.setTrustAnchors(&xYa);
  espClient.setInsecure();
  BearSSL::X509List x509(publicCert().c_str());
  BearSSL::PrivateKey* privKey = new BearSSL::PrivateKey(privateCert().c_str());
  espClient.setClientRSACert(&x509, privKey);

  serialLog("Check connection...");
  if (!espClient.connect(yandex_iot_endpoint, yandex_iot_port)) {
    serialLog("Connection to iot failed. error = " +
              espClient.getLastSSLError());
  } else {
    serialLog("Connection to iot is tested.");
  }
}

bool isTimeInited() { return time(nullptr) > 1000000000; }

void checkWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {  // Ожидаем подключения к Wi-Fi
    serialLog(esp_command_connect_wifi, "Waiting wi-fi");
    delay(1000);
    i++;
    if (i > 15) {
      serialLog("Unable to connect wifi, restart.");
      ESP.restart();
    }
  }

  // Выводим информацию о подключении
  serialLog(esp_command_inited, "Connected to " + ssid());
  serialLog(WiFi.localIP().toString());
}

void setupOTA() {
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    serialLog("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() { serialLog("End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

void setup() {
  if (true) {
    Serial.begin(115200);  // для общения с ардуино
  } else {
    Serial.begin(9600);  // для отладки
    delay(3000);
  }
  while (!Serial) {
  }

  serialLog(esp_command_start_work, "Start working");

  // Устанавливаем Wi-Fi модуль в режим клиента (STA)
  WiFi.mode(WIFI_STA);
  // Устанавливаем ssid и пароль от сети, подключаемся
  WiFi.begin(ssid(), wifiPasswork());
  checkWifi();
  setupOTA();
  // setupWifiClient();

  serialLog("Start sync time");
  configTime("UTC+3", "ntp6.ntp-servers.net	", "1.ru.pool.ntp.org",
             "ntp.ix.ru");
  while (!isTimeInited()) {
    serialLog("Waiting for time sync...");
    delay(1000);
  }
  serialTimeSynced();

  sett.begin();
  sett.onBuild(build);
  sett.onUpdate(updateSettings);

  timerStateSend.setDuration(repeatIntervalStateSend);
}

void sendMessageToIOT() {
  serialLog("Start sending message...");
  if (true) {
    serialLog("Communication with iot is turned off, exit.");
    return;
  }
  HTTPClient https;
  String url = "https://";
  url += yandex_iot_endpoint;
  url += "/iot-devices/v1/devices/";
  url += getDeviceId();
  url += "/publish";
  https.begin(espClient, url);

  https.addHeader("Content-Type", "application/json");
  https.addHeader("Connection", "close");
  https.addHeader("Authorization", "Bearer " + getBearerAuthKey());

  JsonDocument subDoc;
  subDoc["timestamp"] = getTimestamp();
  subDoc["ip_address"] = WiFi.localIP().toString();
  subDoc["rssi"] = WiFi.RSSI();

  JsonDocument doc;
  doc["topic"] = "events";
  // doc["data"] = 1;
  doc["data"] = "test";

  String json;
  serializeJson(doc, json);

  serialLog("post: " + json);
  int httpCode = https.POST(json);
  serialLog("got result " + httpCode);
  String response = https.getString();
  serialLog("Response: " + response);
  https.end();
}

void processMessage(String message) {
  logger.println(getTimestamp());
  logger.println(message);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error != DeserializationError::Ok) {
    serialError((String) "Can't deserialize " + error.c_str());
    serialLog(message);
  } else {
    const char* command = doc[command_key];
    if (strcmp(command, arduino_command_state) == 0) {
      State state = deserializeState(doc);
      lastState = state;
      stateRecieved = true;

      if (false) {
        // в случае если нужна отладка того, что десериализовалось
        JsonDocument des = serializeState(state);
        String outJson;
        serializeJson(des, outJson);
        serialLog((String) "Deserialized (and serialized): " + outJson);
      } else {
        serialLog("Processed new state");
      }
    } else {
      serialError((String) "Unknown command " + command);
      serialLog(message);
    }
  }
}

void loop() {
  ArduinoOTA.handle();

  comm.communicationTick(); //todo вынести в другой поток? <<<<<<<<<<<<<<<<<<<<<<
  if (comm.communicationHasMessage()) {
    processMessage(comm.communicationGetMessage());
    return;
  }
  // if (Serial.available() > 0) {
  //   String message = Serial.readStringUntil('\n');
  //   message.trim();
  //   processMessage(message);
  //   return;
  // }

  sett.tick();

  if (timerStateSend.expired()) {
    timerStateSend.setDuration(repeatIntervalStateSend);
    sendMessageToIOT();
    return;
  }

  if (false) {
    processMessage(
        "{\"temperature\":24.08141,\"humidity\":36.35157,\"pompIsOn\":true,"
        "\"plants\":[{\"isOn\":10,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":10,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":10,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023},{\"isOn\":0,\"plantName\":\"\",\"parrots\":0,"
        "\"originalValue\":1023}],\"command\":\"arduino_state_update\"}");
    delay(2000);
  }
}