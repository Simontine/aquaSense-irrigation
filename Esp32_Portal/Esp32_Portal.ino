#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <math.h>
#include <string.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <time.h>
#include <DHT.h>
#include "firebase.h"
#include "test.h"

constexpr uint8_t OLED_SDA_PIN = 21;
constexpr uint8_t OLED_SCL_PIN = 22;

constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t TANK_TRIG_PIN = 16;
constexpr uint8_t TANK_ECHO_PIN = 17;
constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t PUMP_RELAY_PIN = 26;
constexpr uint8_t DHT_TYPE = DHT11;
constexpr int SOIL_DRY_RAW = 3200;
constexpr int SOIL_WET_RAW = 1400;
constexpr float TANK_EMPTY_DISTANCE_CM = 20.0f;
constexpr float TANK_FULL_DISTANCE_CM = 3.0f;
constexpr uint8_t TANK_LOW_LIMIT_PERCENT = 18;
constexpr uint8_t DEFAULT_MOISTURE_THRESHOLD = 38;
constexpr unsigned long SENSOR_INTERVAL_MS = 2200;
constexpr unsigned long FIREBASE_STATUS_INTERVAL_MS = 5000;
constexpr unsigned long FIREBASE_COMMAND_INTERVAL_MS = 2000;
constexpr uint64_t COMMAND_MAX_AGE_MS = 30000;

DHT dht(DHT_PIN, DHT_TYPE);

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

float soilPercent = NAN;
float tankPercent = NAN;
float tankDistanceCm = NAN;
float temperatureC = NAN;
float humidityPercent = NAN;
bool soilValid = false;
bool tankValid = false;
bool climateValid = false;
bool pumpOn = false;
String irrigationMode = "auto";
uint8_t moistureThreshold = DEFAULT_MOISTURE_THRESHOLD;
unsigned long lastSensorRead = 0;
unsigned long lastFirebaseStatus = 0;
unsigned long lastFirebaseCommand = 0;
String lastFirebaseCommandId;
bool timeConfigured = false;
unsigned long lastOledDraw = 0;

String firebaseUrl(const char* path) {
  String url = FIREBASE_DATABASE_URL;
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  url += path;
  url += ".json";

  if (strlen(FIREBASE_AUTH_TOKEN) > 0) {
    url += "?auth=";
    url += FIREBASE_AUTH_TOKEN;
  }
  return url;
}

bool firebaseRequest(const char* method, const char* path, const String& body,
                     String* responseBody = nullptr) {
  if (WiFi.status() != WL_CONNECTED || strlen(FIREBASE_ROOT_CA) == 0) {
    return false;
  }

  WiFiClientSecure client;
  client.setCACert(FIREBASE_ROOT_CA);

  HTTPClient request;
  request.setTimeout(3000);
  if (!request.begin(client, firebaseUrl(path))) {
    return false;
  }

  int responseCode;
  if (strcmp(method, "PATCH") == 0) {
    responseCode = request.PATCH(body);
  } else if (strcmp(method, "PUT") == 0) {
    responseCode = request.PUT(body);
  } else {
    responseCode = request.GET();
  }

  if (responseCode < 0) {
    Serial.printf("Firebase request failed at %s: %s\n",
                  path, request.errorToString(responseCode).c_str());
  } else if (responseCode >= 400) {
    Serial.printf("Firebase returned HTTP %d at %s\n", responseCode, path);
  }
  if (responseCode >= 200 && responseCode < 300 && responseBody != nullptr) {
    *responseBody = request.getString();
  }
  request.end();
  return responseCode >= 200 && responseCode < 300;
}

float readSoilMoisturePercent() {
  const int raw = analogRead(SOIL_PIN);
  if (raw <= 20 || raw >= 4075 || SOIL_DRY_RAW == SOIL_WET_RAW) {
    return NAN;
  }

  const float percent =
    static_cast<float>(SOIL_DRY_RAW - raw) * 100.0f /
    static_cast<float>(SOIL_DRY_RAW - SOIL_WET_RAW);
  return constrain(percent, 0.0f, 100.0f);
}

float readTankDistanceCm() {
  digitalWrite(TANK_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TANK_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TANK_TRIG_PIN, LOW);

  const unsigned long duration = pulseIn(TANK_ECHO_PIN, HIGH, 30000UL);
  if (duration == 0) {
    return NAN;
  }

  const float distanceCm = static_cast<float>(duration) * 0.0343f / 2.0f;
  return distanceCm >= 2.0f && distanceCm <= 400.0f ? distanceCm : NAN;
}

float tankLevelPercentFromDistance(float distanceCm) {
  if (isnan(distanceCm) ||
      TANK_EMPTY_DISTANCE_CM <= TANK_FULL_DISTANCE_CM ||
      TANK_EMPTY_DISTANCE_CM > 400.0f ||
      TANK_FULL_DISTANCE_CM < 2.0f) {
    return NAN;
  }

  const float percent =
    (TANK_EMPTY_DISTANCE_CM - distanceCm) * 100.0f /
    (TANK_EMPTY_DISTANCE_CM - TANK_FULL_DISTANCE_CM);
  return constrain(percent, 0.0f, 100.0f);
}

void setPump(bool requested) {
  if (requested &&
      (!tankValid || tankPercent < TANK_LOW_LIMIT_PERCENT)) {
    requested = false;
  }
  if (requested && irrigationMode == "auto" && !soilValid) {
    requested = false;
  }

  pumpOn = requested;
  digitalWrite(PUMP_RELAY_PIN, pumpOn ? LOW : HIGH);
}

void updateSensors() {
  const float nextSoilPercent = readSoilMoisturePercent();
  soilValid = !isnan(nextSoilPercent);
  if (soilValid) {
    soilPercent = nextSoilPercent;
  }

  tankDistanceCm = readTankDistanceCm();
  const float nextTankPercent = tankLevelPercentFromDistance(tankDistanceCm);
  tankValid = !isnan(nextTankPercent);
  if (tankValid) {
    tankPercent = nextTankPercent;
  }

  const float nextTemperature = dht.readTemperature();
  const float nextHumidity = dht.readHumidity();
  climateValid = !isnan(nextTemperature) && !isnan(nextHumidity);
  if (climateValid) {
    temperatureC = nextTemperature;
    humidityPercent = nextHumidity;
  } else {
    Serial.println("DHT read failed - check wiring on GPIO4 and pull-up");
  }

  if (!tankValid || tankPercent < TANK_LOW_LIMIT_PERCENT) {
    setPump(false);
  }
}

void applyAutomation() {
  if (!tankValid || tankPercent < TANK_LOW_LIMIT_PERCENT) {
    setPump(false);
    return;
  }
  if (irrigationMode != "auto") {
    return;
  }
  if (!soilValid) {
    setPump(false);
    return;
  }
  if (!pumpOn && soilPercent < moistureThreshold) {
    setPump(true);
  } else if (pumpOn && soilPercent >= moistureThreshold + 5) {
    setPump(false);
  }
}

bool publishFirebaseStatus() {
  const time_t now = time(nullptr);
  if (now < 1700000000) {
    return false;
  }

  JsonDocument sensors;
  sensors["soilMoisture"] = soilValid ? roundf(soilPercent) : NAN;
  sensors["tankLevel"] = tankValid ? roundf(tankPercent) : NAN;
  sensors["tankDistanceCm"] = isnan(tankDistanceCm) ? NAN : tankDistanceCm;
  sensors["temp"] = climateValid ? temperatureC : NAN;
  sensors["humidity"] = climateValid ? humidityPercent : NAN;

  JsonDocument status;
  status["soilPercent"] = soilValid ? roundf(soilPercent) : 0;
  status["tankPercent"] = tankValid ? roundf(tankPercent) : 0;
  status["tankDistanceCm"] = isnan(tankDistanceCm) ? NAN : tankDistanceCm;
  status["temperatureC"] = climateValid ? temperatureC : NAN;
  status["humidityPercent"] = climateValid ? humidityPercent : NAN;
  status["pumpOn"] = pumpOn;
  status["mode"] = irrigationMode;
  status["moistureThreshold"] = moistureThreshold;
  status["soilValid"] = soilValid;
  status["tankValid"] = tankValid;
  status["climateValid"] = climateValid;
  status["updatedAt"] = static_cast<uint64_t>(now) * 1000;

  String sensorBody;
  String statusBody;
  serializeJson(sensors, sensorBody);
  serializeJson(status, statusBody);
  const bool sensorsWritten =
    firebaseRequest("PATCH", "/sensorData", sensorBody);
  const bool statusWritten =
    firebaseRequest("PATCH", "/irrigation/status", statusBody);
  return sensorsWritten && statusWritten;
}

void pollFirebaseCommand() {
  const time_t now = time(nullptr);
  if (now < 1700000000) {
    return;
  }

  String responseBody;
  if (!firebaseRequest("GET", "/irrigation/command", "", &responseBody)) {
    return;
  }

  JsonDocument command;
  if (deserializeJson(command, responseBody) || !command.is<JsonObject>()) {
    return;
  }

  const char* commandId = command["id"] | "";
  if (commandId[0] == '\0' || lastFirebaseCommandId == commandId) {
    return;
  }

  const uint64_t sentAt = command["sentAt"] | 0ULL;
  const uint64_t nowMs = static_cast<uint64_t>(now) * 1000;
  if (sentAt == 0 || sentAt > nowMs || nowMs - sentAt > COMMAND_MAX_AGE_MS) {
    return;
  }
  lastFirebaseCommandId = commandId;

  if (command["moistureThreshold"].is<int>()) {
    moistureThreshold = static_cast<uint8_t>(
      constrain(command["moistureThreshold"].as<int>(), 15, 75)
    );
  }

  const char* requestedMode = command["mode"] | "";
  if (strcmp(requestedMode, "auto") == 0 ||
      strcmp(requestedMode, "manual") == 0) {
    irrigationMode = requestedMode;
    if (irrigationMode == "auto") {
      setPump(false);
    }
  }

  if (command["pump"].is<bool>()) {
    const bool requestedPump = command["pump"].as<bool>();
    if (requestedPump && irrigationMode != "manual") {
      Serial.println("Firebase pump command rejected: switch to manual mode first");
    } else if (requestedPump &&
               (!tankValid || tankPercent < TANK_LOW_LIMIT_PERCENT)) {
      Serial.println("Firebase pump command rejected: tank is unsafe");
    } else {
      setPump(requestedPump);
    }
  }

  applyAutomation();
  if (!publishFirebaseStatus()) {
    Serial.println("Firebase status publish failed after command");
  }
}

void addCorsHeaders() {
  WiFiSetup::server.sendHeader("Access-Control-Allow-Origin", "*");
  WiFiSetup::server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  WiFiSetup::server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  WiFiSetup::server.sendHeader("Cache-Control", "no-store");
}

void sendJson(int statusCode, JsonDocument& document) {
  String body;
  serializeJson(document, body);
  addCorsHeaders();
  WiFiSetup::server.send(statusCode, "application/json", body);
}

void handleLocalStatus() {
  JsonDocument response;
  response["soilPercent"] = soilValid ? roundf(soilPercent) : 0;
  response["tankPercent"] = tankValid ? roundf(tankPercent) : 0;
  if (isnan(tankDistanceCm)) {
    response["tankDistanceCm"] = nullptr;
  } else {
    response["tankDistanceCm"] = tankDistanceCm;
  }
  if (climateValid) {
    response["temperatureC"] = temperatureC;
    response["humidityPercent"] = humidityPercent;
  } else {
    response["temperatureC"] = nullptr;
    response["humidityPercent"] = nullptr;
  }
  response["pumpOn"] = pumpOn;
  response["mode"] = irrigationMode;
  response["moistureThreshold"] = moistureThreshold;
  response["soilValid"] = soilValid;
  response["tankValid"] = tankValid;
  response["climateValid"] = climateValid;
  response["updatedAt"] = millis();
  sendJson(200, response);
}

void handleLocalConfig() {
  JsonDocument request;
  if (deserializeJson(request, WiFiSetup::server.arg("plain"))) {
    JsonDocument response;
    response["error"] = "Invalid JSON body";
    sendJson(400, response);
    return;
  }

  if (request["moistureThreshold"].is<int>()) {
    moistureThreshold = static_cast<uint8_t>(
      constrain(request["moistureThreshold"].as<int>(), 15, 75)
    );
  }

  JsonDocument response;
  response["ok"] = true;
  response["moistureThreshold"] = moistureThreshold;
  sendJson(200, response);
}

void handleLocalControl() {
  JsonDocument request;
  if (deserializeJson(request, WiFiSetup::server.arg("plain"))) {
    JsonDocument response;
    response["error"] = "Invalid JSON body";
    sendJson(400, response);
    return;
  }

  if (request["mode"].is<const char*>()) {
    const String requestedMode = request["mode"].as<String>();
    if (requestedMode != "auto" && requestedMode != "manual") {
      JsonDocument response;
      response["error"] = "Mode must be auto or manual";
      sendJson(400, response);
      return;
    }
    irrigationMode = requestedMode;
    if (irrigationMode == "auto") {
      setPump(false);
    }
  }

  if (request["pump"].is<bool>()) {
    if (irrigationMode != "manual") {
      JsonDocument response;
      response["error"] = "Switch to manual mode before operating the pump";
      sendJson(409, response);
      return;
    }

    const bool requestedPump = request["pump"].as<bool>();
    if (requestedPump &&
        (!tankValid || tankPercent < TANK_LOW_LIMIT_PERCENT)) {
      JsonDocument response;
      response["error"] = "Pump blocked: tank reading is invalid or below the safe limit";
      sendJson(409, response);
      return;
    }
    setPump(requestedPump);
  }

  applyAutomation();
  JsonDocument response;
  response["ok"] = true;
  response["pumpOn"] = pumpOn;
  response["mode"] = irrigationMode;
  sendJson(200, response);
}

void registerIrrigationRoutes() {
  WiFiSetup::server.on("/api/status", HTTP_GET, handleLocalStatus);
  WiFiSetup::server.on("/api/config", HTTP_POST, handleLocalConfig);
  WiFiSetup::server.on("/api/control", HTTP_POST, handleLocalControl);
  WiFiSetup::server.on("/api/config", HTTP_OPTIONS, []() {
    addCorsHeaders();
    WiFiSetup::server.send(204);
  });
  WiFiSetup::server.on("/api/control", HTTP_OPTIONS, []() {
    addCorsHeaders();
    WiFiSetup::server.send(204);
  });
}

enum class Screen { Hotspot, Connecting, Connected, Dashboard };
Screen screen = Screen::Hotspot;
unsigned long connectedShownAt = 0;

void drawWelcome() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB12_tf);
  oled.drawStr(24, 28, "Welcome");
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(4, 48, "AquaSense Irrigation");
  oled.sendBuffer();
}

void drawHotspotInfo(const char* ssid, const String& ip) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(4, 10, "Connect to hotspot:");
  oled.drawHLine(0, 14, 128);
  oled.drawStr(4, 28, ssid);
  oled.drawStr(4, 44, "Portal IP:");
  oled.drawStr(4, 58, ip.c_str());
  oled.sendBuffer();
}

// 6x10 font fits 21 characters per 128 px line.
void drawConnecting(const String& ssid, unsigned dots) {
  constexpr unsigned kCharsPerLine = 21;
  String name = ssid.substring(0, kCharsPerLine);
  String progress;
  const unsigned count = dots % (kCharsPerLine * 2 + 1);
  for (unsigned i = 0; i < count; ++i) progress += '.';

  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 10, "Connecting to:");
  oled.drawHLine(0, 14, 128);
  oled.drawStr(0, 28, name.c_str());
  oled.drawStr(0, 44, progress.substring(0, kCharsPerLine).c_str());
  if (progress.length() > kCharsPerLine) {
    oled.drawStr(0, 58, progress.substring(kCharsPerLine).c_str());
  }
  oled.sendBuffer();
}

void drawWifiConnected() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB12_tf);
  oled.drawStr(34, 28, "WiFi");
  oled.drawStr(14, 46, "Connected");
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Starting AquaSense Irrigation...");

  pinMode(PUMP_RELAY_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  pumpOn = false;

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);
  pinMode(TANK_TRIG_PIN, OUTPUT);
  pinMode(TANK_ECHO_PIN, INPUT);
  digitalWrite(TANK_TRIG_PIN, LOW);
  dht.begin();
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oled.begin();
  oled.setContrast(180);

  drawWelcome();
  delay(10000);

  // Shows the hotspot screen while the AP starts.
  drawHotspotInfo("Starting...", "--");

  // Start your existing Wi-Fi provisioning system.
  WiFiSetup::setProgressCallback(drawConnecting);
  registerIrrigationRoutes();
  WiFiSetup::begin();
}

void updateOled() {
  if (millis() - lastOledDraw < 40) {
    return;
  }
  lastOledDraw = millis();

  const bool connected = WiFi.status() == WL_CONNECTED;

  if (!connected && WiFiSetup::isShowingProgress()) {
    screen = Screen::Connecting;
  } else if (screen == Screen::Connecting && !connected) {
    screen = Screen::Hotspot;
  }

  if ((screen == Screen::Hotspot || screen == Screen::Connecting) && connected) {
    screen = Screen::Connected;
    connectedShownAt = millis();
  } else if (screen == Screen::Connected &&
             millis() - connectedShownAt >= 3000) {
    screen = Screen::Dashboard;
  } else if (screen == Screen::Dashboard && !connected &&
             WiFiSetup::isApActive()) {
    screen = Screen::Hotspot;
  }

  if (screen == Screen::Hotspot) {
    drawHotspotInfo("AquaSense Setup", WiFi.softAPIP().toString());
    return;
  }
  if (screen == Screen::Connecting) {
    drawConnecting(WiFiSetup::connectingSsid(), WiFiSetup::connectingDots());
    return;
  }
  if (screen == Screen::Connected) {
    drawWifiConnected();
    return;
  }

  char line[32];
  char soilValue[8];
  char tankValue[8];

  oled.clearBuffer();

  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(4, 10, "AquaSense Irrigation");
  oled.drawHLine(0, 14, 128);

  // Row 1: temperature and humidity
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(4, 24, "TEMP");
  oled.drawStr(55, 24, "HUMI");
  oled.setFont(u8g2_font_7x14B_tf);
  if (!climateValid || isnan(temperatureC)) {
    oled.drawStr(4, 38, "--.-C");
  } else {
    snprintf(line, sizeof(line), "%.1fC", temperatureC);
    oled.drawStr(4, 38, line);
  }
  if (!climateValid || isnan(humidityPercent)) {
    oled.drawStr(55, 38, "--%");
  } else {
    snprintf(line, sizeof(line), "%.0f%%", humidityPercent);
    oled.drawStr(55, 38, line);
  }

  // Row 2: live readings from the local sensors.
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(4, 49, "SOIL");
  oled.drawStr(55, 49, "TANK");
  oled.setFont(u8g2_font_7x14B_tf);
  if (!soilValid || isnan(soilPercent)) {
    snprintf(soilValue, sizeof(soilValue), "--%%");
  } else {
    snprintf(soilValue, sizeof(soilValue), "%.0f%%", soilPercent);
  }
  if (!tankValid || isnan(tankPercent)) {
    snprintf(tankValue, sizeof(tankValue), "--%%");
  } else {
    snprintf(tankValue, sizeof(tankValue), "%.0f%%", tankPercent);
  }
  oled.drawStr(4, 63, soilValue);
  oled.drawStr(55, 63, tankValue);

  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(90, 24, "Status");
  oled.drawStr(90, 49, "PUMP");
  oled.setFont(u8g2_font_7x14B_tf);
  char modeLabel[6];
  if (irrigationMode == "manual") {
    snprintf(modeLabel, sizeof(modeLabel), "MAN");
  } else {
    snprintf(modeLabel, sizeof(modeLabel), "%.4s", irrigationMode.c_str());
    for (char* c = modeLabel; *c; ++c) *c = toupper(*c);
  }
  oled.drawStr(90, 38, modeLabel);
  oled.drawStr(90, 63, pumpOn ? "ON" : "OFF");

  oled.sendBuffer();
}


void loop() {
  // Preserve the existing Wi-Fi provisioning and connection handling.
  WiFiSetup::handle();

  if (WiFi.status() == WL_CONNECTED && !timeConfigured) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    timeConfigured = true;
  }

  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    updateSensors();
    applyAutomation();
  }

  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastFirebaseCommand >= FIREBASE_COMMAND_INTERVAL_MS) {
    lastFirebaseCommand = millis();
    pollFirebaseCommand();
  }

  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastFirebaseStatus >= FIREBASE_STATUS_INTERVAL_MS) {
    lastFirebaseStatus = millis();
    if (!publishFirebaseStatus() && time(nullptr) >= 1700000000) {
      Serial.println("Firebase status publish failed");
    }
  }

  updateOled();
  delay(2);
}
