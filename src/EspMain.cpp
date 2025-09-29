#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SettingsGyver.h>
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

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
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
  Serial.println("Setup secure wifi...");
  // BearSSL::X509List xYa(ya_sert);
  // espClient.setTrustAnchors(&xYa);
  espClient.setInsecure();
  BearSSL::X509List x509(publicCert().c_str());
  BearSSL::PrivateKey* privKey = new BearSSL::PrivateKey(privateCert().c_str());
  espClient.setClientRSACert(&x509, privKey);

  Serial.println("Check connection...");
  if (!espClient.connect(yandex_iot_endpoint, yandex_iot_port)) {
    Serial.println("Connection to iot failed.");
    Serial.println(espClient.getLastSSLError());
  } else {
    Serial.println("Connection to iot is tested.");
  }
}

void setup(void) {
  Serial.begin(9600);
  Serial.println("\nStart working");

  // Устанавливаем Wi-Fi модуль в режим клиента (STA)
  WiFi.mode(WIFI_STA);
  // Устанавливаем ssid и пароль от сети, подключаемся
  WiFi.begin(ssid(), wifiPasswork());

  while (WiFi.status() != WL_CONNECTED) {  // Ожидаем подключения к Wi-Fi
    delay(500);
    Serial.print(".");
  }

  // Выводим информацию о подключении
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("Setup time");
  configTime(3 * 3600, 0, "ntp6.ntp-servers.net	", "1.ru.pool.ntp.org",
             "ntp.ix.ru");
  Serial.println(getTimestamp());

  // setupWifiClient();

  sett.begin();
  sett.onBuild(build);
  sett.onUpdate(updateSettings);
}

void loop(void) {
  if (true) {
    sett.tick();
    return;
  }
  Serial.println("Start sending message...");
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

  Serial.println("post: " + json);
  int httpCode = https.POST(json);
  Serial.println("got result");
  Serial.println(httpCode);
  String response = https.getString();
  Serial.println("Response: " + response);
  https.end();

  delay(5000);
}