#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <U8g2lib.h>
#include "test.h"

constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t OLED_SDA_PIN = 21;
constexpr uint8_t OLED_SCL_PIN = 22;

DHT dht(DHT_PIN, DHT11);
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

float temperature = NAN;
float humidity = NAN;
unsigned long lastDHTRead = 0;
unsigned long lastOledDraw = 0;

// Separate server for the welcome page.
// Uses port 8080 so it does not conflict with test.h's port 80.
WebServer welcomeServer(8080);

const char WELCOME_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#123d2b">
  <title>Smart Irrigation | Welcome</title>
  <style>
    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: Arial, Helvetica, sans-serif;
      color: #172b24;
      background: #eef3f0;
    }

    header {
      padding: 55px 22px 75px;
      color: white;
      background: linear-gradient(135deg, #123d2b, #16764a);
    }

    .container {
      max-width: 960px;
      margin: auto;
    }

    .brand {
      display: flex;
      align-items: center;
      gap: 12px;
      font-weight: bold;
      letter-spacing: 1px;
      text-transform: uppercase;
    }

    .logo {
      padding: 12px;
      border-radius: 14px;
      background: #ffffff20;
      font-size: 25px;
    }

    h1 {
      margin: 38px 0 12px;
      font-size: clamp(34px, 7vw, 54px);
    }

    header p {
      max-width: 620px;
      color: #d4e9dc;
      line-height: 1.7;
    }

    .status {
      display: inline-block;
      margin-top: 18px;
      padding: 11px 16px;
      border: 1px solid #ffffff40;
      border-radius: 30px;
      background: #ffffff12;
      font-size: 14px;
    }

    main {
      padding: 0 20px 40px;
    }

    .panel {
      position: relative;
      margin-top: -35px;
      padding: 28px;
      border: 1px solid #e0e9e3;
      border-radius: 20px;
      background: white;
      box-shadow: 0 12px 35px #123d2b12;
    }

    .panel > p {
      color: #64746b;
      line-height: 1.7;
    }

    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 18px;
      margin-top: 25px;
    }

    .card {
      padding: 22px;
      border: 1px solid #e0e9e3;
      border-radius: 16px;
      background: #fbfdfb;
    }

    .icon {
      margin-bottom: 15px;
      font-size: 28px;
    }

    .card h3 {
      margin: 0 0 10px;
    }

    .card p {
      color: #64746b;
      font-size: 14px;
      line-height: 1.7;
    }

    .network {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 18px;
      margin-top: 25px;
      padding: 18px;
      border-radius: 12px;
      background: #eef7f0;
    }

    .network small {
      display: block;
      margin-bottom: 7px;
      color: #64746b;
    }

    .network strong {
      overflow-wrap: anywhere;
      color: #145b39;
    }

    footer {
      padding: 0 20px 30px;
      color: #7a8980;
      text-align: center;
      font-size: 12px;
    }
  </style>
</head>

<body>
  <header>
    <div class="container">
      <div class="brand">
        <div class="logo">&#127807;</div>
        <span>Smart Irrigation</span>
      </div>

      <h1>You're connected.</h1>

      <p>
        Welcome to your Smart Irrigation controller.
        Your ESP32 has successfully connected to your Wi-Fi
        network and is ready for your smart irrigation project.
      </p>

      <div class="status">&#10003; Wi-Fi connection established</div>
    </div>
  </header>

  <main>
    <div class="container">
      <section class="panel">
        <h2>Welcome to your control centre</h2>
        <p>
          Your controller is online on your local network.
          This page confirms connectivity. Sensor readings and
          pump controls can be integrated as the project develops.
        </p>

        <div class="grid">
          <div class="card">
            <div class="icon">&#128225;</div>
            <h3>Network connected</h3>
            <p>Your ESP32 has joined your configured Wi-Fi router.</p>
          </div>

          <div class="card">
            <div class="icon">&#127793;</div>
            <h3>Smart irrigation</h3>
            <p>Ready for soil moisture, tank level and pump monitoring.</p>
          </div>

          <div class="card">
            <div class="icon">&#128274;</div>
            <h3>Local access</h3>
            <p>Open this page from a device connected to the same router.</p>
          </div>
        </div>

        <div class="network">
          <div>
            <small>Wi-Fi network</small>
            <strong id="ssid">Loading...</strong>
          </div>
          <div>
            <small>Controller IP address</small>
            <strong id="ip">Loading...</strong>
          </div>
        </div>
      </section>
    </div>
  </main>

  <footer>
    Smart Irrigation Controller &bull; ESP32 Local Web Interface
  </footer>

  <script>
    fetch('/api/status')
      .then(response => response.json())
      .then(data => {
        document.getElementById('ssid').textContent = data.ssid || 'Connected';
        document.getElementById('ip').textContent = data.ip || location.hostname;
      })
      .catch(() => {
        document.getElementById('ip').textContent = location.hostname;
      });
  </script>
</body>
</html>
)HTML";

void configureWelcomeServer() {
  welcomeServer.on("/", HTTP_GET, []() {
    welcomeServer.sendHeader("Cache-Control", "no-store");
    welcomeServer.send_P(200, "text/html", WELCOME_PAGE);
  });

  welcomeServer.on("/api/status", HTTP_GET, []() {
    if (WiFi.status() != WL_CONNECTED) {
      welcomeServer.send(
        503,
        "application/json",
        "{\"connected\":false}"
      );
      return;
    }

    String json = "{\"connected\":true,\"ssid\":\"";
    json += WiFi.SSID();
    json += "\",\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\"}";

    welcomeServer.sendHeader("Cache-Control", "no-store");
    welcomeServer.send(200, "application/json", json);
  });

  welcomeServer.onNotFound([]() {
    welcomeServer.send(404, "text/plain", "Page not found");
  });
}

bool welcomeServerStarted = false;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Starting Smart Irrigation...");

  // Start your existing Wi-Fi provisioning system.
  WiFiSetup::begin();

  configureWelcomeServer();

  dht.begin();
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oled.begin();
  oled.setContrast(180);
}

void updateOled() {
  static int x = 5;
  static int direction = 1;

  // Read DHT11 every 2 seconds
  if (millis() - lastDHTRead >= 2000 || lastDHTRead == 0) {
    lastDHTRead = millis();

    float newHumidity = dht.readHumidity();
    float newTemperature = dht.readTemperature();

    if (!isnan(newHumidity) && !isnan(newTemperature)) {
      humidity = newHumidity;
      temperature = newTemperature;

      Serial.print("Temperature: ");
      Serial.print(temperature, 1);
      Serial.print(" C | Humidity: ");
      Serial.print(humidity, 1);
      Serial.println(" %");
    } else {
      Serial.println("DHT11 read failed. Check wiring.");
    }
  }

  if (millis() - lastOledDraw < 40) {
    return;
  }
  lastOledDraw = millis();

  const bool connected = WiFi.status() == WL_CONNECTED;
  const char* wifiText = connected ? "Connected"
                         : WiFiSetup::isConnecting() ? "Connecting..."
                         : WiFiSetup::isApActive() ? "Setup mode"
                                                   : "Disconnected";
  String ipText = connected ? WiFi.localIP().toString()
                  : WiFiSetup::isApActive() ? WiFi.softAPIP().toString()
                                            : String("--");

  char line[32];

  oled.clearBuffer();

  // Wi-Fi status and IP address
  oled.setFont(u8g2_font_6x10_tf);
  snprintf(line, sizeof(line), "WiFi: %s", wifiText);
  oled.drawStr(4, 10, line);
  snprintf(line, sizeof(line), "IP: %s", ipText.c_str());
  oled.drawStr(4, 22, line);
  oled.drawHLine(4, 26, 120);

  // Temperature
  oled.drawStr(5, 37, "TEMPERATURE");
  oled.setFont(u8g2_font_helvB12_tf);
  if (isnan(temperature)) {
    oled.drawStr(5, 54, "--.- C");
  } else {
    snprintf(line, sizeof(line), "%.1f C", temperature);
    oled.drawStr(5, 54, line);
  }

  // Humidity
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(85, 37, "HUMIDITY");
  oled.setFont(u8g2_font_helvB12_tf);
  if (isnan(humidity)) {
    oled.drawStr(85, 54, "--%");
  } else {
    snprintf(line, sizeof(line), "%.0f%%", humidity);
    oled.drawStr(85, 54, line);
  }

  // Animated progress bar
  oled.drawFrame(3, 58, 122, 6);
  oled.drawBox(x, 60, 12, 2);

  oled.sendBuffer();

  x += direction * 3;
  if (x >= 110) {
    x = 110;
    direction = -1;
  }
  if (x <= 5) {
    x = 5;
    direction = 1;
  }
}

void loop() {
  // Preserve the existing Wi-Fi provisioning and connection handling.
  WiFiSetup::handle();

  // Start the welcome server only after router Wi-Fi connects.
  if (WiFi.status() == WL_CONNECTED && !welcomeServerStarted) {
    welcomeServer.begin();
    welcomeServerStarted = true;

    Serial.println();
    Serial.println("==================================");
    Serial.println(" SMART IRRIGATION ONLINE");
    Serial.print(" Wi-Fi: ");
    Serial.println(WiFi.SSID());
    Serial.print(" Welcome page: http://");
    Serial.print(WiFi.localIP());
    Serial.println(":8080/");
    Serial.println("==================================");
  }

  if (welcomeServerStarted) {
    welcomeServer.handleClient();
  }

    updateOled();

  delay(2);
}
