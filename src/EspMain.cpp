#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SettingsGyver.h>
#include <State.h>
#include <WiFiClientSecure.h>
#include <time.h>

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
  response["command"] = command;
  response["timestamp"] = getTimestamp();
  response["log"] = s;

  String responseJson;
  serializeJson(response, responseJson);
  Serial.println(responseJson);
}

void serialLog(const String& s) { serialLog(esp_command_log, s); }

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

  while (WiFi.status() != WL_CONNECTED) {  // Ожидаем подключения к Wi-Fi
    serialLog(esp_command_connect_wifi, "Waiting wi-fi");
    delay(1000);
  }

  // Выводим информацию о подключении
  serialLog(esp_command_inited, "Connected to " + ssid());
  serialLog(WiFi.localIP().toString());
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
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    serialLog(command);
    return;
  }

  if (true) {
    return;
  }

  serialLog("Start sending message...");
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

  delay(5000);
}