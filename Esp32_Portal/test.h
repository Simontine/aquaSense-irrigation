#ifndef TEST_H
#define TEST_H


#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

namespace WiFiSetup {

// Wi-Fi provisioning configuration
static const uint16_t DNS_PORT = 53;
static const unsigned long CONNECT_TIMEOUT_MS = 30000UL;
static const unsigned long RETRY_INTERVAL_MS = 10000UL;

static const char* AP_SSID = "Smart-Irrigation-Setup";
static const char* AP_PASSWORD = "";
static const char* AP_IP_TEXT = "192.168.4.1";

static WebServer server(80);
static DNSServer dns;
static Preferences preferences;

static String savedSSID;
static String savedPassword;

static bool apActive = false;
static bool serverStarted = false;
static bool connecting = false;

static unsigned long connectStartedAt = 0;
static unsigned long lastRetryAt = 0;

// --------------------------------------------------
// Setup page
// --------------------------------------------------
static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport"
        content="width=device-width, initial-scale=1">
  <title>Smart Irrigation Setup</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: 24px 14px;
      background: #eef3f0;
      color: #172b24;
      font-family: Arial, sans-serif;
    }
    .card {
      max-width: 420px;
      margin: 30px auto;
      padding: 25px;
      background: white;
      border-radius: 18px;
      box-shadow: 0 8px 28px #00000012;
    }
    h1 { font-size: 24px; margin-bottom: 8px; }
    p { line-height: 1.5; color: #52645b; }
    label {
      display: block;
      margin: 18px 0 7px;
      font-weight: bold;
    }
    input {
      width: 100%;
      padding: 13px;
      border: 1px solid #ccd8d0;
      border-radius: 9px;
      font-size: 16px;
    }
    button {
      width: 100%;
      margin-top: 22px;
      padding: 14px;
      background: #16764a;
      color: white;
      border: 0;
      border-radius: 9px;
      font-size: 16px;
      font-weight: bold;
    }
    .note {
      font-size: 13px;
      margin-top: 18px;
    }
  </style>
</head>
<body>
  <div class="card">
    <h1>Smart Irrigation</h1>
    <p>Connect your controller to your home or office Wi-Fi.</p>

    <form action="/save" method="POST">
      <label for="ssid">Wi-Fi network name (SSID)</label>
      <input id="ssid" name="ssid" type="text"
             maxlength="32" autocomplete="off"
             placeholder="Enter Wi-Fi name" required>

      <label for="password">Wi-Fi password</label>
      <input id="password" name="password"
             type="password" maxlength="64"
             autocomplete="new-password"
             placeholder="Enter Wi-Fi password">

      <button type="submit">Save and Connect</button>
    </form>

    <p class="note">
      Your Wi-Fi details are stored on this ESP32.
      Keep this setup network password private.
    </p>
  </div>
</body>
</html>
)HTML";

// --------------------------------------------------
// Redirect captive-portal checks to the setup page
// --------------------------------------------------
void redirectToPortal() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader(
    "Location",
    String("http://") + AP_IP_TEXT + "/",
    true
  );
  server.send(302, "text/plain", "Opening Wi-Fi setup...");
}

void configureRoutes() {
  Serial.println("[Routes] Registering /");

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PAGE);
  });

  Serial.println("[Routes] Registering Android routes");

  server.on("/generate_204", HTTP_GET, redirectToPortal);
  server.on("/gen_204", HTTP_GET, redirectToPortal);

  Serial.println("[Routes] Registering Apple routes");

  server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
  server.on("/library/test/success.html", HTTP_GET, redirectToPortal);
  server.on("/success.txt", HTTP_GET, redirectToPortal);
  server.on("/canonical.html", HTTP_GET, redirectToPortal);

  Serial.println("[Routes] Registering Windows routes");

  server.on("/connecttest.txt", HTTP_GET, redirectToPortal);
  server.on("/redirect", HTTP_GET, redirectToPortal);
  server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
  server.on("/connecttest.txt", HTTP_HEAD, redirectToPortal);
  server.on("/ncsi.txt", HTTP_HEAD, redirectToPortal);

  Serial.println("[Routes] Registering other connectivity routes");

  server.on("/check_network_status.txt", HTTP_GET, redirectToPortal);
  server.on("/fwlink", HTTP_GET, redirectToPortal);

  Serial.println("[Routes] Registering POST /save");

  server.on("/save", HTTP_POST, []() {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");

    newSSID.trim();

    if (newSSID.length() == 0 || newSSID.length() > 32 ||
        newPassword.length() > 64) {
      server.send(
        400, "text/html",
        "<h2>Invalid Wi-Fi details</h2><a href='/'>Go back</a>"
      );
      return;
    }

    preferences.begin("wifi-config", false);
    preferences.putString("ssid", newSSID);
    preferences.putString("password", newPassword);
    preferences.end();

    savedSSID = newSSID;
    savedPassword = newPassword;

    server.send(
      200, "text/html",
      "<h2>Wi-Fi details saved</h2>"
      "<p>Restarting connection...</p>"
    );

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    connecting = true;
    connectStartedAt = millis();
    lastRetryAt = millis();
  });

  Serial.println("[Routes] Registering fallback");

  server.onNotFound(redirectToPortal);

  Serial.println("[Routes] All routes registered");
}

// --------------------------------------------------
// Start the configuration hotspot
// --------------------------------------------------
void startAP() {
  Serial.println("[WiFiSetup] Starting setup access point...");

  WiFi.mode(WIFI_AP_STA);
  delay(200);

  bool started = WiFi.softAP(AP_SSID);

  if (started) {
    Serial.print("[WiFiSetup] Setup AP IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[WiFiSetup] Failed to start setup AP.");
  }
}

// --------------------------------------------------
// Start connection using saved router credentials
// --------------------------------------------------
void connectSavedCredentials() {
  if (savedSSID.isEmpty()) {
    Serial.println("[WiFiSetup] No saved Wi-Fi credentials.");
    startAP();
    return;
  }

  Serial.printf("[WiFiSetup] Connecting to: %s\n",
                savedSSID.c_str());

  // Ensure the Wi-Fi interface is initialized.
  WiFi.mode(WIFI_AP_STA);

  // Stop any previous station connection before reconfiguring it.
  WiFi.disconnect(false, false);
  delay(300);

  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFiSetup] Router connected successfully.");
    Serial.print("[WiFiSetup] IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFiSetup] Router connection timed out.");
    Serial.println("[WiFiSetup] Starting provisioning AP.");
    startAP();
  }
}

// --------------------------------------------------
// Stop the setup hotspot after router connection
// --------------------------------------------------
void stopAP() {
  if (!apActive) return;

  dns.stop();
  WiFi.softAPdisconnect(true);
  apActive = false;

  WiFi.mode(WIFI_STA);

  Serial.println("Router connected; setup hotspot stopped.");
}

// --------------------------------------------------
// Initialize Wi-Fi setup
// --------------------------------------------------
void begin() {
  Serial.println("[WiFiSetup] begin() started");

  // Load saved credentials.
  Serial.println("[WiFiSetup] Loading saved credentials");

  preferences.begin("wifi-config", true);
  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");
  preferences.end();

  // Initialize the Wi-Fi stack before starting the HTTP server.
  Serial.println("[WiFiSetup] Initializing Wi-Fi");

  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // Register HTTP routes.
  Serial.println("[WiFiSetup] Configuring HTTP routes");
  configureRoutes();

  // Start the HTTP server after Wi-Fi initialization.
  Serial.println("[WiFiSetup] Starting HTTP server");
  server.begin();
  serverStarted = true;

  Serial.println("[WiFiSetup] HTTP server started");

  // Start provisioning or connect using saved credentials.
  if (savedSSID.length() == 0) {
    Serial.println("[WiFiSetup] No saved credentials; starting AP");
    startAP();
  } else {
    Serial.println("[WiFiSetup] Connecting with saved credentials");
    connectSavedCredentials();
  }

  Serial.println("[WiFiSetup] Initialization complete");
}

bool isConnecting() { return connecting; }
bool isApActive() { return apActive; }

// --------------------------------------------------
// Service HTTP, DNS, and Wi-Fi state
// Call continuously from loop().
// --------------------------------------------------
void handle() {
  if (serverStarted) {
    server.handleClient();
  }

  if (apActive) {
    dns.processNextRequest();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (connecting) {
      connecting = false;

      Serial.println("Connected to router.");
      Serial.print("Router IP: ");
      Serial.println(WiFi.localIP());

      // Stop the setup network once the router is connected.
      stopAP();
    }
    return;
  }

  // If router connection timed out, keep/start the setup hotspot.
  if (connecting &&
      millis() - connectStartedAt >= CONNECT_TIMEOUT_MS) {
    connecting = false;
    Serial.println("Router connection timed out.");

    if (!apActive) {
      startAP();
    }
  }

  // Retry router connection periodically while keeping AP available.
  if (apActive &&
      savedSSID.length() > 0 &&
      !connecting &&
      millis() - lastRetryAt >= RETRY_INTERVAL_MS) {
    lastRetryAt = millis();

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    connecting = true;
    connectStartedAt = millis();

    Serial.println("Retrying router connection...");
  }
}

} // namespace WiFiSetup

#endif