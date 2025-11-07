#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SettingsGyver.h>
#include <State.h>
#include <Timer.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <ArduinoOTA.h>

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

SettingsGyver sett("My Settings");
int slider;
String input;
bool updatedSettings = false;

Timer timerStateSend;
const Duration repeatIntervalStateSend = Timer::Minutes(30);
bool stateSended = true;
State lastState;

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

void serialLog(const String& command, const String& s) {
  JsonDocument response;
  response[command_key] = command;
  response["timestamp"] = getTimestamp();
  response["log"] = s;

  String responseJson;
  serializeJson(response, responseJson);
  Serial.println(responseJson);
}

void serialLog(const String& s) { serialLog(esp_command_log, s); }

void serialError(const String& s) { serialLog(esp_command_log, s); }

void build(sets::Builder& b) {
  b.Slider("My slider", 0, 50, 1, "ml", &slider);
  b.Input("My input", &input);
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

void processCommand(String command, JsonDocument& doc) {
  command.toLowerCase();
  Serial.print("Processing command: ");
  Serial.println(command);

  JsonDocument response;
  response["device_id"] = getDeviceId();
  response["timestamp"] = getTimestamp();

  if (command == "do_smth") {
    // just do it
    response["status"] = "success";
    response["message"] = "Relay turned ON";
  } else {
    response["status"] = "error";
    response["message"] = "Unknown command: " + command;
  }

  // Отправка ответа
  String responseJson;
  serializeJson(response, responseJson);
  // mqttClient.publish(topic_events, responseJson.c_str());
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
  ArduinoOTA.onEnd([]() {
    serialLog("End");
  });
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
  serialLog("Time was synchronized");

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
  serialLog(message);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error != DeserializationError::Ok) {
    serialError((String) "Can't deserialize " + error.c_str());
  } else {
    const char* command = doc[command_key];
    if (strcmp(command, arduino_command_state) == 0) {
      State state = deserializeState(doc);
      lastState = state;
      stateSended = false;

      if (false) {
        // в случае если нужна отладка того, что десериализовалось
        JsonDocument des = serializeState(state);
        String outJson;
        serializeJson(des, outJson);
        serialLog((String) "Deserialized (and serialized): " + outJson);
      }
    } else {
      serialError((String) "Unknown command " + command);
    }
  }
}

void loop() {
  ArduinoOTA.handle();

  if (Serial.available() > 0) {
    String message = Serial.readStringUntil('\n');
    message.trim();
    processMessage(message);
    return;
  }

  sett.tick();

  if (timerStateSend.expired()) {
    timerStateSend.setDuration(repeatIntervalStateSend);
    sendMessageToIOT();
    return;
  }

  if (false) {
    processMessage(
        "{\"temperature\":-45,\"humidity\":0,\"plants\":[{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":505},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":505},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":504},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":504},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":503},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":503},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":503},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":502},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":502},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":502},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":501},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":501},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":74,\"originalValue\":501},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":75,\"originalValue\":500},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":75,\"originalValue\":500},{\"isOn\":0,"
        "\"plantName\":\"\",\"parrots\":75,\"originalValue\":500}],\"command\":"
        "\"arduino_state_update\"}");
    delay(2000);
  }
}