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
constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t DHT_TYPE = DHT11;
constexpr int SOIL_DRY_RAW = 3200;
constexpr int SOIL_WET_RAW = 1400;
constexpr unsigned long SENSOR_PUBLISH_INTERVAL_MS = 5000;

DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastSensorPublish = 0;

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

float soilPercent = NAN;
float tankPercent = NAN;
float temperatureC = NAN;
float humidityPercent = NAN;
bool soilValid = false;
bool tankValid = false;
bool climateValid = false;
bool pumpOn = false;
String irrigationMode = "--";
uint64_t updatedAt = 0;
JsonDocument firebaseSensorData;
JsonDocument firebaseIrrigationStatus;
bool sensorDataAvailable = false;
bool irrigationStatusAvailable = false;
String sensorDataError = "Waiting for Firebase data";
String irrigationStatusError = "Waiting for Firebase data";
bool firebaseTimeSyncRequested = false;
unsigned long lastFirebaseRead = 0;
unsigned long lastOledDraw = 0;

bool readFiniteNumber(JsonVariantConst value, float& result) {
  if (value.isNull() || !value.is<float>()) {
    return false;
  }

  const float parsed = value.as<float>();
  if (!isfinite(parsed)) {
    return false;
  }

  result = parsed;
  return true;
}

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

bool fetchFirebaseRecord(const char* path, JsonDocument& destination, String& error) {
  if (WiFi.status() != WL_CONNECTED || strlen(FIREBASE_ROOT_CA) == 0) {
    destination.clear();
    error = WiFi.status() != WL_CONNECTED
      ? "Wi-Fi disconnected"
      : "Firebase root certificate is not configured";
    return false;
  }

  WiFiClientSecure client;
  client.setCACert(FIREBASE_ROOT_CA);

  HTTPClient request;
  request.setTimeout(3000);
  if (!request.begin(client, firebaseUrl(path))) {
    destination.clear();
    error = "Could not start Firebase request";
    return false;
  }

  const int responseCode = request.GET();
  if (responseCode < 200 || responseCode >= 300) {
    String message = "Firebase request failed";
    if (responseCode >= 0) {
      message = "Firebase returned HTTP ";
      message += responseCode;
    } else {
      message = request.errorToString(responseCode);
    }
    request.end();
    destination.clear();
    error = message;
    Serial.printf("Firebase request failed at %s: %s\n", path, error.c_str());
    return false;
  }

  const String responseBody = request.getString();
  request.end();

  JsonDocument fetched;
  const DeserializationError parseError = deserializeJson(fetched, responseBody);
  if (parseError || !fetched.is<JsonObject>()) {
    destination.clear();
    error = "Firebase data is missing or invalid";
    Serial.printf("Firebase data at %s is missing or invalid\n", path);
    return false;
  }
  destination.set(fetched.as<JsonVariantConst>());
  error = "";
  return true;
}

bool patchFirebase(const char* path, const String& body) {
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

  const int code = request.PATCH(body);
  if (code < 0) {
    Serial.printf("Firebase PATCH failed at %s: %s\n", path, request.errorToString(code).c_str());
  } else if (code >= 400) {
    Serial.printf("Firebase PATCH returned HTTP %d at %s\n", code, path);
  }
  request.end();
  return code >= 200 && code < 300;
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

// Reads local sensors and writes their values to the paths the dashboard reads.
void publishSensorReadings() {
  if (time(nullptr) < 1700000000) {
    return;
  }

  soilPercent = readSoilMoisturePercent();
  soilValid = isfinite(soilPercent);

  const float t = dht.readTemperature();
  const float h = dht.readHumidity();
  const bool ok = !isnan(t) && !isnan(h);

  JsonDocument sensors;
  JsonDocument status;
  sensors["soilMoisture"] = soilValid ? roundf(soilPercent) : NAN;
  status["soilPercent"] = soilValid ? roundf(soilPercent) : 0;
  status["soilValid"] = soilValid;
  if (ok) {
    sensors["temp"] = t;
    sensors["humidity"] = h;
    status["temperatureC"] = t;
    status["humidityPercent"] = h;
  } else {
    Serial.println("DHT read failed - check wiring on GPIO4 and pull-up");
  }
  status["climateValid"] = ok;
  status["updatedAt"] = static_cast<uint64_t>(time(nullptr)) * 1000;

  String statusBody;
  serializeJson(status, statusBody);
  String sensorBody;
  serializeJson(sensors, sensorBody);
  const bool sensorsSent = patchFirebase("/sensorData", sensorBody);
  const bool sent = patchFirebase("/irrigation/status", statusBody);
  if (!sensorsSent) {
    Serial.println("Firebase sensorData publish failed");
  }
  if (!sent) {
    Serial.println("Firebase irrigation/status publish failed");
  }
  Serial.printf(
    "Sensor publish %s (soil %s, DHT %s)\n",
    sensorsSent && sent ? "ok" : "failed",
    soilValid ? "valid" : "invalid",
    ok ? "valid" : "invalid"
  );
}

bool firstNumber(JsonVariantConst first, JsonVariantConst second,
                 JsonVariantConst third, float& result) {
  return readFiniteNumber(first, result) ||
         readFiniteNumber(second, result) ||
         readFiniteNumber(third, result);
}

bool flagOrTrue(JsonVariantConst value) {
  return !value.is<bool>() || value.as<bool>();
}

// Shows every numeric value Firebase holds; the *Valid flags report whether
// the firmware considered the sensor reading trustworthy.
void updateDisplayedValues() {
  JsonVariantConst status = firebaseIrrigationStatus.as<JsonVariantConst>();
  JsonVariantConst sensors = firebaseSensorData.as<JsonVariantConst>();
  JsonVariantConst none;

  soilPercent = NAN;
  tankPercent = NAN;
  temperatureC = NAN;
  humidityPercent = NAN;
  firstNumber(status["soilPercent"], sensors["soilMoisture"], none, soilPercent);
  firstNumber(status["tankPercent"], sensors["tankLevel"], none, tankPercent);
  firstNumber(status["temperatureC"], status["temperature"], sensors["temp"], temperatureC);
  firstNumber(status["humidityPercent"], status["humidity"], sensors["humidity"], humidityPercent);

  soilValid = flagOrTrue(status["soilValid"]);
  tankValid = flagOrTrue(status["tankValid"]);
  climateValid = flagOrTrue(status["climateValid"]);
  pumpOn = status["pumpOn"].is<bool>() && status["pumpOn"].as<bool>();

  const char* mode = status["mode"] | "";
  irrigationMode = mode[0] != '\0' ? mode : "--";
  updatedAt = status["updatedAt"].is<uint64_t>()
    ? status["updatedAt"].as<uint64_t>()
    : 0;
}

bool refreshFirebaseData() {
  if (WiFi.status() != WL_CONNECTED) {
    sensorDataAvailable = false;
    irrigationStatusAvailable = false;
    sensorDataError = "Wi-Fi disconnected";
    irrigationStatusError = "Wi-Fi disconnected";
    firebaseSensorData.clear();
    firebaseIrrigationStatus.clear();
    updateDisplayedValues();
    return false;
  }

  if (time(nullptr) < 1700000000) {
    if (!firebaseTimeSyncRequested) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      firebaseTimeSyncRequested = true;
    }
    sensorDataAvailable = false;
    irrigationStatusAvailable = false;
    sensorDataError = "Waiting for device time synchronization";
    irrigationStatusError = sensorDataError;
    firebaseSensorData.clear();
    firebaseIrrigationStatus.clear();
    updateDisplayedValues();
    return false;
  }

  sensorDataAvailable = fetchFirebaseRecord(
    "/sensorData",
    firebaseSensorData,
    sensorDataError
  );
  irrigationStatusAvailable = fetchFirebaseRecord(
    "/irrigation/status",
    firebaseIrrigationStatus,
    irrigationStatusError
  );
  updateDisplayedValues();
  return sensorDataAvailable || irrigationStatusAvailable;
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

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);
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
  if (isnan(temperatureC)) {
    oled.drawStr(4, 38, "--.-C");
  } else {
    snprintf(line, sizeof(line), "%.1fC", temperatureC);
    oled.drawStr(4, 38, line);
  }
  if (isnan(humidityPercent)) {
    oled.drawStr(55, 38, "--%");
  } else {
    snprintf(line, sizeof(line), "%.0f%%", humidityPercent);
    oled.drawStr(55, 38, line);
  }

  // Row 2: sensor readings from Firebase.
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(4, 49, "SOIL");
  oled.drawStr(55, 49, "TANK");
  oled.setFont(u8g2_font_7x14B_tf);
  if (isnan(soilPercent)) {
    snprintf(soilValue, sizeof(soilValue), "--%%");
  } else {
    snprintf(soilValue, sizeof(soilValue), "%.0f%%", soilPercent);
  }
  if (isnan(tankPercent)) {
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
  oled.drawStr(90, 63, irrigationStatusAvailable ? (pumpOn ? "ON" : "OFF") : "--");

  oled.sendBuffer();
}


void loop() {
  // Preserve the existing Wi-Fi provisioning and connection handling.
  WiFiSetup::handle();

  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastSensorPublish >= SENSOR_PUBLISH_INTERVAL_MS) {
    lastSensorPublish = millis();
    publishSensorReadings();
  }

  if (WiFi.status() == WL_CONNECTED &&
      (lastFirebaseRead == 0 || millis() - lastFirebaseRead >= 5000)) {
    lastFirebaseRead = millis();
    refreshFirebaseData();
  } else if (WiFi.status() != WL_CONNECTED) {
    if (sensorDataAvailable || irrigationStatusAvailable ||
        sensorDataError != "Wi-Fi disconnected" ||
        irrigationStatusError != "Wi-Fi disconnected") {
      sensorDataAvailable = false;
      irrigationStatusAvailable = false;
      sensorDataError = "Wi-Fi disconnected";
      irrigationStatusError = sensorDataError;
      firebaseSensorData.clear();
      firebaseIrrigationStatus.clear();
      updateDisplayedValues();
    }
    firebaseTimeSyncRequested = false;
  }

  updateOled();
  delay(2);
}
