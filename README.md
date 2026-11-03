# aquaSense Smart Irrigation

A garden irrigation controller with an ESP32 sensor/pump unit and a React dashboard. The ESP32 connects to Firebase Realtime Database over Wi-Fi; the dashboard reads status and queues control commands through Firebase, so the browser and device do not need to share a local network. The dashboard includes sensor readings, irrigation controls, notifications, and local rule-based irrigation guidance.

## Hardware assumptions

- ESP32 DevKit V1 (ESP32-WROOM-32)
- Capacitive analog soil-moisture sensor
- Waterproof ultrasonic distance sensor (for example, JSN-SR04T) mounted above the tank
- DHT11 temperature/humidity module
- 2.42-inch SSD1309 128x64 OLED display (I2C)
- Relay module appropriate for the pump load
- Pump with a separate, correctly rated power supply

### Pin map

| Signal | ESP32 GPIO | Notes |
| --- | ---: | --- |
| Soil sensor analog output | 34 | ADC input-only; power the probe from 3.3 V when supported |
| Ultrasonic sensor TRIG | 16 | Digital output |
| Ultrasonic sensor ECHO | 17 | Digital input; use a voltage divider/level shifter if the sensor's echo output is 5 V |
| DHT11 data | 4 | Use a pull-up if the module does not include one |
| OLED SDA | 21 | SSD1309 I2C interface; use the module's specified supply voltage |
| OLED SCL | 22 | SSD1309 I2C interface |
| Relay input | 26 | Firmware assumes an active-low relay; change `setPump()` if yours differs. A hardware pull-up can help keep it off during reset. |

Connect sensor grounds to ESP32 ground. Never connect a pump directly to an ESP32 GPIO or power it from the board. Use a relay/driver and a separate supply rated for the pump; use appropriate flyback suppression for a DC pump. Keep water away from exposed electronics, and have a qualified person handle mains-voltage wiring.

## Firmware setup

1. Install PlatformIO (VS Code extension or PlatformIO Core).
2. Create `Esp32 Code/firmware/secrets.h` with your own device configuration. This file is ignored by Git; keep it local and never commit it. For example:

	```cpp
	#define WIFI_SSID "your-2.4GHz-network"
	#define WIFI_PASSWORD "your-wifi-password"
	#define FIREBASE_DATABASE_URL "https://your-project-default-rtdb.firebaseio.com"
	#define FIREBASE_AUTH_TOKEN ""
	```

	Use a Firebase auth token only if your database rules require one. The firmware expects the token to be available to the device; tokens embedded in firmware are not a secure way to protect production data.
3. Open `Esp32 Code` as a PlatformIO project, then build and upload to the ESP32. Serial Monitor runs at 115200 baud.
4. Confirm the ESP32 publishes a fresh `irrigation/status` record in Firebase. The dashboard uses the database URL in `.env.local` or the project default.

PlatformIO Core commands from the project root:

```sh
pio run -d "Esp32 Code"
pio run -d "Esp32 Code" -t upload
pio device monitor -d "Esp32 Code"
```

The ESP32 publishes sensor readings to `sensorData` and control state to `irrigation/status` every five seconds. It polls `irrigation/command` every two seconds and ignores commands older than 30 seconds. Firebase time synchronization is required for command expiry and dashboard freshness checks. The firmware bundles Google Trust Services Root R1, which is verified for the configured Firebase endpoint; define `FIREBASE_ROOT_CA` locally only if your endpoint uses a different trusted chain. `FIREBASE_AUTH_TOKEN` is optional when database rules allow unauthenticated access and required otherwise.

Set the dashboard database URL in `.env.local` if it differs from the default. For local development, the optional auth token can be set here too:

```sh
VITE_FIREBASE_DATABASE_URL=https://your-project-default-rtdb.firebaseio.com
VITE_FIREBASE_AUTH_TOKEN=your-short-lived-user-id-token
```

`VITE_` values are included in the browser bundle and are visible to every dashboard user. Never put a service-account key, database secret, or durable privileged credential there. The dashboard's email/password screen is a local demo account stored in this browser; it is not Firebase Authentication and does not restrict Firebase access. The dashboard's adaptive guidance is computed from local rules and sensor readings; it does not call an AI service.

Configure Firebase Realtime Database rules deliberately. The dashboard needs read access to `sensorData` and `irrigation/status`, and write access to `irrigation/command`; the ESP32 needs to write sensor/status data and read commands. `Esp32_Portal` now also reads Firebase settings from `Esp32 Code/firmware/secrets.h` when that local file exists, publishes sensor and irrigation status, and polls fresh commands to control the pump. It uses the same bundled root certificate and retains its captive Wi-Fi provisioning portal and OLED setup/connection/dashboard screens. Do not make pump commands publicly writable. For production, use Firebase Authentication with rules tied to authenticated users or a trusted backend to authorize commands. The current direct-to-Firebase browser and firmware clients are intended for development with appropriately restricted rules, not as a production credential boundary.

The SSD1309 display shows soil moisture, tank level, air temperature, air humidity, pump state, and irrigation mode. Sensor values display as `--` when the corresponding reading is invalid.

The pump relay starts OFF at boot. In auto mode the pump turns on below the configured moisture threshold and turns off after the soil recovers by 5 percentage points. Pump operation is refused when the tank level is invalid or below 18%, including in manual mode; Firebase commands do not bypass these firmware checks. The portal sketch uses the same relay and ultrasonic sensor pins as the controller sketch. If sensor calibration or relay polarity differs from your hardware, test with the pump disconnected first.

### Calibration

Edit the soil calibration constants near the top of `Esp32 Code/firmware/firmware.ino` or `Esp32_Portal/Esp32_Portal.ino`, depending on which ESP32 sketch you use:

- `SOIL_DRY_RAW` and `SOIL_WET_RAW`: record the analog values in dry and well-watered soil; sensor outputs vary substantially.
- `TANK_EMPTY_DISTANCE_CM` and `TANK_FULL_DISTANCE_CM`: measure from the sensor face to the water surface with the tank empty and full. Set both values in the active firmware sketch; level remains invalid and the pump stays locked until these are configured. The firmware maps the distances to 0–100%; verify the 18% cutoff before connecting the pump.
- `TANK_LOW_LIMIT_PERCENT`: minimum safe tank percentage before the pump is inhibited.

Mount the ultrasonic sensor above the water, pointing down, and use a waterproof model intended for a humid tank environment. Check the sensor's minimum range against your tank depth. Power the module according to its specifications, share ground with the ESP32, and never connect a 5 V ECHO output directly to an ESP32 GPIO; use a voltage divider or logic-level shifter. Validate the distance at empty, full, and the 18% pump cutoff before enabling the pump.

## Dashboard

From the project root, install dependencies and start the Vite development server:

```sh
npm install
npm run dev
```

Open the Vite URL printed in the terminal. Sensor readings and irrigation status refresh from Firebase every five seconds. Mode, threshold, and pump controls enqueue commands for the ESP32; commands require a fresh device status and are confirmed by the device. Notifications report connection and sensor conditions. The optional adaptive threshold suggestion is rule-based and can be applied from the dashboard. History records sensor samples and pump state changes in the current browser while the dashboard is open, retaining up to seven days; observed pump runtime is accumulated since tracking began. Runtime is not a calibrated estimate of water volume, and history is not synchronized to Firebase or other browsers. Build and type-check a production bundle with `npm run build`; preview it locally with `npm run preview`.

## Firebase data contract

- `sensorData`: current `soilMoisture`, `tankLevel`, `tankDistanceCm`, `temp`, and `humidity` readings.
- `irrigation/status`: readings including `tankDistanceCm`, sensor validity flags, pump state, mode, moisture threshold, and Unix-millisecond `updatedAt`.
- `irrigation/command`: dashboard writes an `id` and `sentAt` with optional `mode`, `pump`, or `moistureThreshold` fields; the ESP32 applies fresh commands and enforces its local safety checks.
