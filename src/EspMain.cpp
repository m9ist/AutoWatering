#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

// Блок из SecretHolder, чтобы не заводить лишний .h файл, нужно там реализовать эти методы
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

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);

  // Преобразование payload в строку
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  Serial.print("Message: ");
  Serial.println(message);

  // Парсинг JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Обработка команд
  if (doc.containsKey("command")) {
    String command = doc["command"].as<String>();
    processCommand(command, doc);  // todo
  }
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

  setupWifiClient();
  // setupMQTT();
}

void loop(void) {
  if (true) {
    Serial.println("work done.");
    delay(5000);
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