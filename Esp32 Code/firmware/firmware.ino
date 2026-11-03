#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>
#include <Wire.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID "NTINI"
#define WIFI_PASSWORD "Mm@phut1"
#endif

#ifndef FIREBASE_DATABASE_URL
#define FIREBASE_DATABASE_URL "https://irrigation-system-ffb92-default-rtdb.firebaseio.com"
#endif

#ifndef FIREBASE_AUTH_TOKEN
#define FIREBASE_AUTH_TOKEN ""
#endif

#ifndef FIREBASE_ROOT_CA
static const char firebaseRootCa[] PROGMEM = R"FIREBASE_ROOT_CA(
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
)FIREBASE_ROOT_CA";
#define FIREBASE_ROOT_CA firebaseRootCa
#endif

namespace {

// ============================================================
// PIN CONFIGURATION
// ============================================================

constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t TANK_TRIG_PIN = 16;
constexpr uint8_t TANK_ECHO_PIN = 17;
constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t PUMP_RELAY_PIN = 26;

constexpr uint8_t OLED_SDA_PIN = 21;
constexpr uint8_t OLED_SCL_PIN = 22;

constexpr uint8_t DHT_TYPE = DHT11;


// ============================================================
// SENSOR CALIBRATION
// ============================================================

// Soil sensor calibration.
// Higher raw value = drier soil.
// Lower raw value = wetter soil.
constexpr int SOIL_DRY_RAW = 3200;
constexpr int SOIL_WET_RAW = 1400;

// Set these to the sensor-to-water distances measured with the tank empty/full.
// Zero keeps tank level invalid until the installation is calibrated.
constexpr float TANK_EMPTY_DISTANCE_CM = 20.0f;
constexpr float TANK_FULL_DISTANCE_CM = 3.0f;

// Pump will not run below this tank level.
constexpr uint8_t TANK_LOW_LIMIT_PERCENT = 18;


// ============================================================
// TIMING
// ============================================================

constexpr uint32_t SENSOR_INTERVAL_MS = 2200;
constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint32_t FIREBASE_STATUS_INTERVAL_MS = 5000;
constexpr uint32_t FIREBASE_COMMAND_INTERVAL_MS = 2000;
constexpr uint64_t COMMAND_MAX_AGE_MS = 30000;


// ============================================================
// HARDWARE OBJECTS
// ============================================================

WebServer server(80);

DHT dht(
    DHT_PIN,
    DHT_TYPE
);

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE
);


// ============================================================
// SYSTEM STATE
// ============================================================

float soilPercent = 0.0f;
float tankPercent = 0.0f;
float tankDistanceCm = NAN;

float temperatureC = NAN;
float humidityPercent = NAN;

uint8_t moistureThreshold = 38;

bool soilValid = false;
bool tankValid = false;
bool climateValid = false;

bool pumpOn = false;

// "auto" or "manual"
String irrigationMode = "auto";

uint32_t lastSensorRead = 0;
uint32_t lastWifiAttempt = 0;
uint32_t lastFirebaseStatus = 0;
uint32_t lastFirebaseCommand = 0;
String lastFirebaseCommandId;
bool timeConfigured = false;

// ============================================================
// PUMP CONTROL
// ============================================================
//
// This code assumes an ACTIVE-LOW relay:
//
// LOW  = pump ON
// HIGH = pump OFF
//
// If your relay is active HIGH, reverse these values.
// ============================================================

void setPump(bool requested) {

  // ----------------------------------------------------------
  // SAFETY 1:
  // Never run the pump if the tank sensor is invalid.
  // Never run the pump if the tank is below the safe limit.
  // ----------------------------------------------------------

  if (
      requested &&
      (
          !tankValid ||
          tankPercent < TANK_LOW_LIMIT_PERCENT
      )
  ) {
    requested = false;
  }


  // ----------------------------------------------------------
  // SAFETY 2:
  // In automatic mode, a bad soil sensor means pump OFF.
  // ----------------------------------------------------------

  if (
      requested &&
      irrigationMode == "auto" &&
      !soilValid
  ) {
    requested = false;
  }


  // ----------------------------------------------------------
  // Apply pump state.
  // ----------------------------------------------------------

  pumpOn = requested;

  digitalWrite(
      PUMP_RELAY_PIN,
      pumpOn ? LOW : HIGH
  );
}


// ============================================================
// OLED DISPLAY
// ============================================================

void updateDisplay() {

  char soilLine[32];
  char soilValue[8];
  char tankValue[8];
  char climateLine[32];
  char controlLine[32];


  // ----------------------------------------------------------
  // Soil value
  // ----------------------------------------------------------

  if (soilValid) {

    snprintf(
        soilValue,
        sizeof(soilValue),
        "%d",
        static_cast<int>(
            roundf(soilPercent)
        )
    );

  } else {

    snprintf(
        soilValue,
        sizeof(soilValue),
        "--"
    );
  }


  // ----------------------------------------------------------
  // Tank value
  // ----------------------------------------------------------

  if (tankValid) {

    snprintf(
        tankValue,
        sizeof(tankValue),
        "%d",
        static_cast<int>(
            roundf(tankPercent)
        )
    );

  } else {

    snprintf(
        tankValue,
        sizeof(tankValue),
        "--"
    );
  }


  // ----------------------------------------------------------
  // Soil and tank display line
  // ----------------------------------------------------------

  snprintf(
      soilLine,
      sizeof(soilLine),
      "SOIL %s%%  TANK %s%%",
      soilValue,
      tankValue
  );


  // ----------------------------------------------------------
  // Temperature / humidity
  // ----------------------------------------------------------

  if (climateValid) {

    snprintf(
        climateLine,
        sizeof(climateLine),
        "AIR %.0fC  RH %.0f%%",
        temperatureC,
        humidityPercent
    );

  } else {

    snprintf(
        climateLine,
        sizeof(climateLine),
        "AIR --C   RH --%%"
    );
  }


  // ----------------------------------------------------------
  // Pump / mode
  // ----------------------------------------------------------

  snprintf(
      controlLine,
      sizeof(controlLine),
      "PUMP %s  %s",
      pumpOn ? "ON" : "OFF",
      irrigationMode == "auto"
          ? "AUTO"
          : "MANUAL"
  );


  // ----------------------------------------------------------
  // Draw display
  // ----------------------------------------------------------

  display.clearBuffer();

  display.setFont(
      u8g2_font_6x10_tf
  );

  display.drawStr(
      0,
      10,
      "aquaSense IRRIGATION"
  );

  display.drawHLine(
      0,
      13,
      128
  );


  display.setFont(
      u8g2_font_5x7_tf
  );

  display.drawStr(
      0,
      25,
      soilLine
  );

  display.drawStr(
      0,
      37,
      climateLine
  );

  display.drawStr(
      0,
      49,
      controlLine
  );


  // ----------------------------------------------------------
  // Status message
  // ----------------------------------------------------------

  const char *statusLine =
      "SYSTEM READY";


  if (!tankValid) {

    statusLine =
        "TANK SENSOR CHECK";

  } else if (
      tankPercent < TANK_LOW_LIMIT_PERCENT
  ) {

    statusLine =
        "TANK LOW - PUMP LOCKED";

  } else if (
      irrigationMode == "auto" &&
      !soilValid
  ) {

    statusLine =
        "SOIL SENSOR CHECK";

  } else if (pumpOn) {

    statusLine =
        "IRRIGATION ACTIVE";
  }


  display.drawStr(
      0,
      62,
      statusLine
  );

  display.sendBuffer();
}


// ============================================================
// CORS HEADERS
// ============================================================

void addCorsHeaders() {

  server.sendHeader(
      "Access-Control-Allow-Origin",
      "*"
  );

  server.sendHeader(
      "Access-Control-Allow-Methods",
      "GET, POST, OPTIONS"
  );

  server.sendHeader(
      "Access-Control-Allow-Headers",
      "Content-Type"
  );

  server.sendHeader(
      "Cache-Control",
      "no-store"
  );
}


// ============================================================
// SEND JSON RESPONSE
// ============================================================

void sendJson(
    int statusCode,
    JsonDocument &document
) {

  String body;

  serializeJson(
      document,
      body
  );

  addCorsHeaders();

  server.send(
      statusCode,
      "application/json",
      body
  );
}


// ============================================================
// ANALOG SENSOR READINGS
// ============================================================

float readSoilMoisturePercent(uint8_t pin, int dryRaw, int wetRaw) {
  const int raw = analogRead(pin);
  if (raw <= 20 || raw >= 4075 || dryRaw == wetRaw) return NAN;

  const float scaled =
      static_cast<float>(dryRaw - raw) * 100.0f /
      static_cast<float>(dryRaw - wetRaw);
  return constrain(scaled, 0.0f, 100.0f);
}

float readTankDistanceCm() {
  digitalWrite(TANK_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TANK_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TANK_TRIG_PIN, LOW);

  const unsigned long duration = pulseIn(TANK_ECHO_PIN, HIGH, 30000UL);
  if (duration == 0) return NAN;

  const float distanceCm = static_cast<float>(duration) * 0.0343f / 2.0f;
  return distanceCm >= 2.0f && distanceCm <= 400.0f ? distanceCm : NAN;
}

float tankLevelPercentFromDistance(float distanceCm) {
  if (
      isnan(distanceCm) ||
      TANK_EMPTY_DISTANCE_CM <= TANK_FULL_DISTANCE_CM ||
      TANK_EMPTY_DISTANCE_CM > 400.0f ||
      TANK_FULL_DISTANCE_CM < 2.0f
  ) {
    return NAN;
  }

  const float scaled =
      (TANK_EMPTY_DISTANCE_CM - distanceCm) * 100.0f /
      (TANK_EMPTY_DISTANCE_CM - TANK_FULL_DISTANCE_CM);
  return constrain(scaled, 0.0f, 100.0f);
}


// ============================================================
// SENSOR UPDATE
// ============================================================

void updateSensors() {

  // ==========================================================
  // SOIL AND TANK SENSORS
  // ==========================================================

  const float nextSoilPercent = readSoilMoisturePercent(SOIL_PIN, SOIL_DRY_RAW, SOIL_WET_RAW);
  soilValid = !isnan(nextSoilPercent);
  if (soilValid) soilPercent = nextSoilPercent;

  tankDistanceCm = readTankDistanceCm();
  const float nextTankPercent = tankLevelPercentFromDistance(tankDistanceCm);
  tankValid = !isnan(nextTankPercent);
  if (tankValid) tankPercent = nextTankPercent;


  // ==========================================================
  // DHT11
  // ==========================================================

  const float nextTemperature =
      dht.readTemperature();

  const float nextHumidity =
      dht.readHumidity();


  climateValid =
      !isnan(nextTemperature) &&
      !isnan(nextHumidity);


  if (climateValid) {

    temperatureC =
        nextTemperature;

    humidityPercent =
        nextHumidity;
  }
}


// ============================================================
// AUTOMATIC IRRIGATION
// ============================================================

void applyAutomation() {

  // ----------------------------------------------------------
  // Tank protection always wins.
  // ----------------------------------------------------------

  if (
      !tankValid ||
      tankPercent < TANK_LOW_LIMIT_PERCENT
  ) {

    setPump(false);

    return;
  }


  // ----------------------------------------------------------
  // Manual mode.
  // Do not automatically change the pump state.
  // ----------------------------------------------------------

  if (
      irrigationMode != "auto"
  ) {

    return;
  }


  // ----------------------------------------------------------
  // Bad soil sensor.
  // ----------------------------------------------------------

  if (!soilValid) {

    setPump(false);

    return;
  }


  // ----------------------------------------------------------
  // Turn pump ON when soil is too dry.
  // ----------------------------------------------------------

  if (
      !pumpOn &&
      soilPercent < moistureThreshold
  ) {

    setPump(true);
  }


  // ----------------------------------------------------------
  // Turn pump OFF after soil reaches threshold + 5%.
  //
  // Example:
  // threshold = 38%
  //
  // ON below 38%
  // OFF at 43%
  //
  // This provides hysteresis and prevents rapid switching.
  // ----------------------------------------------------------

  else if (
      pumpOn &&
      soilPercent >=
          moistureThreshold + 5
  ) {

    setPump(false);
  }
}


// ============================================================
// GET /api/status
// ============================================================

void handleStatus() {

  JsonDocument response;


  response["soilPercent"] =
      soilValid
          ? roundf(soilPercent)
          : 0;

  response["tankPercent"] =
      tankValid
          ? roundf(tankPercent)
          : 0;
  if (isnan(tankDistanceCm)) {
    response["tankDistanceCm"] = nullptr;
  } else {
    response["tankDistanceCm"] = tankDistanceCm;
  }


  if (climateValid) {

    response["temperatureC"] =
        temperatureC;

    response["humidityPercent"] =
        humidityPercent;

  } else {

    response["temperatureC"] =
        nullptr;

    response["humidityPercent"] =
        nullptr;
  }


  response["pumpOn"] =
      pumpOn;

  response["mode"] =
      irrigationMode;

  response["moistureThreshold"] =
      moistureThreshold;

  response["soilValid"] =
      soilValid;

  response["tankValid"] =
      tankValid;

  response["climateValid"] =
      climateValid;

  response["updatedAt"] =
      millis();


  sendJson(
      200,
      response
  );
}


// ============================================================
// POST /api/config
// ============================================================

void handleConfig() {

  JsonDocument request;


  DeserializationError error =
      deserializeJson(
          request,
          server.arg("plain")
      );


  if (error) {

    JsonDocument response;

    response["error"] =
        "Invalid JSON body";

    sendJson(
        400,
        response
    );

    return;
  }


  // ----------------------------------------------------------
  // Moisture threshold
  // ----------------------------------------------------------

  if (
      request["moistureThreshold"].is<int>()
  ) {

    int requestedThreshold =
        request["moistureThreshold"]
            .as<int>();


    // Keep the threshold in a safe range.
    moistureThreshold =
        static_cast<uint8_t>(
            constrain(
                requestedThreshold,
                15,
                75
            )
        );
  }


  JsonDocument response;


  response["ok"] =
      true;

  response["moistureThreshold"] =
      moistureThreshold;


  sendJson(
      200,
      response
  );
}


// ============================================================
// POST /api/control
// ============================================================

void handleControl() {

  JsonDocument request;


  DeserializationError error =
      deserializeJson(
          request,
          server.arg("plain")
      );


  if (error) {

    JsonDocument response;

    response["error"] =
        "Invalid JSON body";

    sendJson(
        400,
        response
    );

    return;
  }


  // ==========================================================
  // MODE
  // ==========================================================

  if (
      request["mode"].is<const char *>()
  ) {

    const String requestedMode =
        request["mode"].as<String>();


    if (
        requestedMode != "auto" &&
        requestedMode != "manual"
    ) {

      JsonDocument response;

      response["error"] =
          "Mode must be auto or manual";

      sendJson(
          400,
          response
      );

      return;
    }


    irrigationMode =
        requestedMode;


    // When switching to automatic mode,
    // start safely with the pump OFF.
    //
    // The automation logic will decide whether
    // it should turn back on during the next cycle.

    if (
        irrigationMode == "auto"
    ) {

      setPump(false);
    }
  }


  // ==========================================================
  // MANUAL PUMP CONTROL
  // ==========================================================

  if (
      request["pump"].is<bool>()
  ) {

    // Pump control is only allowed in manual mode.
    if (
        irrigationMode != "manual"
    ) {

      JsonDocument response;

      response["error"] =
          "Switch to manual mode before operating the pump";

      sendJson(
          409,
          response
      );

      return;
    }


    const bool requestedPump =
        request["pump"].as<bool>();


    // Never allow manual operation when
    // the tank is unsafe.

    if (
        requestedPump &&
        (
            !tankValid ||
            tankPercent < TANK_LOW_LIMIT_PERCENT
        )
    ) {

      JsonDocument response;

      response["error"] =
          "Pump blocked: tank reading is invalid or below the safe limit";

      sendJson(
          409,
          response
      );

      return;
    }


    setPump(
        requestedPump
    );
  }


  // Tank protection is always checked.
  applyAutomation();


  JsonDocument response;


  response["ok"] =
      true;

  response["pumpOn"] =
      pumpOn;

  response["mode"] =
      irrigationMode;


  sendJson(
      200,
      response
  );
}


// ============================================================
// NOT FOUND / CORS PREFLIGHT
// ============================================================

void handleNotFound() {

  addCorsHeaders();


  // Handle OPTIONS requests.
  if (
      server.method() == HTTP_OPTIONS
  ) {

    server.send(204);

    return;
  }


  server.send(
      404,
      "application/json",
      "{\"error\":\"Not found\"}"
  );
}


// ============================================================
// WI-FI
// ============================================================

void connectWifi() {

  WiFi.mode(
      WIFI_STA
  );


  WiFi.setHostname(
      "irrigation-controller"
  );


  WiFi.begin(
      WIFI_SSID,
      WIFI_PASSWORD
  );


  lastWifiAttempt =
      millis();
}

String firebaseUrl(const char *path) {
    String url = FIREBASE_DATABASE_URL;
    while (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;
    url += ".json";
    if (strlen(FIREBASE_AUTH_TOKEN) > 0) {
        url += "?auth=";
        url += FIREBASE_AUTH_TOKEN;
    }
    return url;
}

bool firebaseRequest(const char *method, const char *path, const String &body, String *responseBody = nullptr) {
    if (WiFi.status() != WL_CONNECTED || strlen(FIREBASE_ROOT_CA) == 0) return false;

    WiFiClientSecure client;
    client.setCACert(FIREBASE_ROOT_CA);

    HTTPClient request;
    request.setTimeout(3000);
    if (!request.begin(client, firebaseUrl(path))) return false;

    int responseCode;
    if (strcmp(method, "PATCH") == 0) {
        responseCode = request.PATCH(body);
    } else if (strcmp(method, "PUT") == 0) {
        responseCode = request.PUT(body);
    } else {
        responseCode = request.GET();
    }

    if (responseCode < 0) {
        Serial.printf("Firebase request failed at %s: %s\n", path, request.errorToString(responseCode).c_str());
    } else if (responseCode >= 400) {
        Serial.printf("Firebase returned HTTP %d at %s\n", responseCode, path);
    }

    if (responseCode >= 200 && responseCode < 300 && responseBody != nullptr) {
        *responseBody = request.getString();
    }
    request.end();
    return responseCode >= 200 && responseCode < 300;
}

bool publishFirebaseStatus() {
    const time_t now = time(nullptr);
    if (now < 1700000000) return false;

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
    const bool sensorsWritten = firebaseRequest("PATCH", "/sensorData", sensorBody);
    const bool statusWritten = firebaseRequest("PATCH", "/irrigation/status", statusBody);
    return sensorsWritten && statusWritten;
}

void pollFirebaseCommand() {
    const time_t now = time(nullptr);
    if (now < 1700000000) return;

    String responseBody;
    if (!firebaseRequest("GET", "/irrigation/command", "", &responseBody)) return;

    JsonDocument command;
    if (deserializeJson(command, responseBody)) return;

    const char *commandId = command["id"] | "";
    if (commandId[0] == '\0' || lastFirebaseCommandId == commandId) return;

    const uint64_t sentAt = command["sentAt"] | 0ULL;
    const uint64_t nowMs = static_cast<uint64_t>(now) * 1000;
    if (sentAt == 0 || sentAt > nowMs || nowMs - sentAt > COMMAND_MAX_AGE_MS) return;
    lastFirebaseCommandId = commandId;

    if (command["moistureThreshold"].is<int>()) {
        moistureThreshold = static_cast<uint8_t>(constrain(command["moistureThreshold"].as<int>(), 15, 75));
    }

    const char *requestedMode = command["mode"] | "";
    if (strcmp(requestedMode, "auto") == 0 || strcmp(requestedMode, "manual") == 0) {
        irrigationMode = requestedMode;
        if (irrigationMode == "auto") setPump(false);
    }

    if (command["pump"].is<bool>()) {
        const bool requestedPump = command["pump"].as<bool>();
        if (requestedPump && irrigationMode != "manual") {
            Serial.println("Firebase pump command rejected: switch to manual mode first");
        } else if (requestedPump && (!tankValid || tankPercent < TANK_LOW_LIMIT_PERCENT)) {
            Serial.println("Firebase pump command rejected: tank is unsafe");
        } else {
            setPump(requestedPump);
        }
    }

    applyAutomation();
    updateDisplay();
    if (!publishFirebaseStatus()) {
        Serial.println("Firebase status publish failed after command");
    }
}

} // namespace


// ============================================================
// SETUP
// ============================================================

void setup() {

  // ----------------------------------------------------------
  // Serial
  // ----------------------------------------------------------

  Serial.begin(
      115200
  );


  // ----------------------------------------------------------
  // PUMP RELAY
  // ----------------------------------------------------------
  //
  // IMPORTANT:
  // Configure the pin BEFORE writing to it.
  //
  // Active LOW relay:
  // HIGH = OFF
  // LOW  = ON
  // ----------------------------------------------------------

  pinMode(
      PUMP_RELAY_PIN,
      OUTPUT
  );


  digitalWrite(
      PUMP_RELAY_PIN,
      HIGH
  );


  pumpOn = false;


  // ----------------------------------------------------------
  // ADC
  // ----------------------------------------------------------

  analogReadResolution(
      12
  );


  analogSetPinAttenuation(
      SOIL_PIN,
      ADC_11db
  );

  pinMode(TANK_TRIG_PIN, OUTPUT);
  pinMode(TANK_ECHO_PIN, INPUT);
  digitalWrite(TANK_TRIG_PIN, LOW);


  // ----------------------------------------------------------
  // DHT11
  // ----------------------------------------------------------

  dht.begin();


  // ----------------------------------------------------------
  // OLED
  // ----------------------------------------------------------

  Wire.begin(
      OLED_SDA_PIN,
      OLED_SCL_PIN
  );


  display.begin();


  display.setContrast(
      180
  );


  updateDisplay();


  // ----------------------------------------------------------
  // WI-FI
  // ----------------------------------------------------------

  connectWifi();


  Serial.println();
  Serial.println(
      "================================"
  );
  Serial.println(
      "AquaSense Irrigation Controller"
  );
  Serial.println(
      "================================"
  );
  Serial.println(
            "Firebase cloud transport enabled"
  );
        if (strlen(FIREBASE_AUTH_TOKEN) == 0) {
                Serial.println("No Firebase auth token set; database rules must permit device access");
    }
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

    // ----------------------------------------------------------
    // WI-FI
    // ----------------------------------------------------------
  if (
      WiFi.status() == WL_CONNECTED
  ) {

        if (!timeConfigured) {
            configTime(0, 0, "pool.ntp.org", "time.google.com");
            timeConfigured = true;
        }

  } else {
    if (
        millis() -
        lastWifiAttempt >=
        WIFI_RETRY_MS
    ) {

      Serial.println(
          "Wi-Fi disconnected. Reconnecting..."
      );


      WiFi.disconnect();


      WiFi.begin(
          WIFI_SSID,
          WIFI_PASSWORD
      );


      lastWifiAttempt =
          millis();
    }
  }

    if (
            WiFi.status() == WL_CONNECTED &&
            millis() - lastFirebaseCommand >= FIREBASE_COMMAND_INTERVAL_MS
    ) {
        lastFirebaseCommand = millis();
        pollFirebaseCommand();
    }

    if (
            WiFi.status() == WL_CONNECTED &&
            millis() - lastFirebaseStatus >= FIREBASE_STATUS_INTERVAL_MS
    ) {
        lastFirebaseStatus = millis();
        publishFirebaseStatus();
    }


  // ----------------------------------------------------------
  // SENSOR UPDATE
  // ----------------------------------------------------------

  if (
      millis() -
      lastSensorRead >=
      SENSOR_INTERVAL_MS
  ) {

    lastSensorRead =
        millis();


    updateSensors();


    applyAutomation();


    updateDisplay();
  }
}