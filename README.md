# ESP32 Electronic Pedometer

An ESP32-based smart electronic pedometer with real-time step counting, an onboard OLED display, persistent NVS flash storage, and an integrated Web Dashboard for step statistics and pace monitoring.

---

## 📋 Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Hardware Components & Pinout](#hardware-components--pinout)
- [Project File Structure](#project-file-structure)
- [Step Detection Algorithm](#step-detection-algorithm)
- [Web Interface & HTTP Endpoints](#web-interface--http-endpoints)
- [Dependencies & Libraries](#dependencies--libraries)
- [Configuration & Setup](#configuration--setup)
- [License](#license)

---

## 📖 Overview

This project implements a wearable / portable step counter using the **ESP32** microcontroller and the **MPU6050** 6-axis motion sensor. Accelerometer measurements are filtered and analyzed in real time to register physical steps.

The device provides two interfaces:
1. **Physical Display**: An I2C SSD1306 128×64 OLED screen showing connection status and the current step tally.
2. **Web Dashboard**: An embedded responsive web server hosting a dark-themed monitoring interface with live step count, cadence / pace (steps per minute), and a 15-minute historical bar chart rendered on an HTML5 canvas.

Step counts and historical statistics are automatically persisted to ESP32 Non-Volatile Storage (NVS Flash) using the `Preferences` library, ensuring data is retained even after power loss or device reboot.

---

## ✨ Key Features

- **Accurate Step Detection**: Uses Euclidean acceleration vector magnitude with an exponential moving average (IIR low-pass filter) combined with Z-axis jerk detection and time-window debouncing.
- **Dual Display Interface**:
  - Local SSD1306 128×64 Monochrome OLED.
  - Remote responsive Web UI accessible via any web browser on the local Wi-Fi network.
- **Cadence & History Tracking**:
  - Measures pace (steps/min) calculated across rolling 10-second intervals.
  - 15-minute history buffer (90 intervals of 10 seconds each) plotted visually.
- **NVS Data Persistence**:
  - Instant write-on-step detection for total step count.
  - Periodic background checkpointing of historical interval data to ESP32 Flash memory.
- **RESTful API**: JSON endpoint (`/data.json`) for easy integration into home automation systems or third-party dashboards.
- **Auxiliary SPI ADC Support**: Includes driver routines for Texas Instruments ADS130B02 2-channel ADC (`Temperature_LM75_Derived.h`).

---

## 🔌 Hardware Components & Pinout

### Required Hardware

- **ESP32 Development Board** (e.g., ESP32 DevKit V1)
- **MPU6050** 6-axis IMU (Accelerometer & Gyroscope)
- **SSD1306 OLED Display** (128×64 pixels, I2C interface, address `0x3C`)
- Breadboard and jumper wires

### Default Wiring & Pin Mapping

#### I2C Bus (MPU6050 & SSD1306 OLED)

| Component Pin | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **SDA** | **GPIO 21** | I2C Data line (400 kHz) |
| **SCL** | **GPIO 22** | I2C Clock line (400 kHz) |
| **VCC** | **3.3V / 5V** | Power supply |
| **GND** | **GND** | Ground |

#### VSPI Bus (Auxiliary ADS130B02 ADC Module)

| Component Pin | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **CS** | **GPIO 5** | Chip Select |
| **SCLK** | **GPIO 18** | SPI Serial Clock |
| **MISO** | **GPIO 19** | Master In Slave Out |
| **MOSI** | **GPIO 23** | Master Out Slave In |

---

## 📁 Project File Structure

```text
Electronic_Pedometer/
│
├── TemperatureAlarm_copy_20260905141710.ino   # Main application sketch (Pedometer firmware,
│                                               # web server, OLED display, and NVS storage)
│
├── Temperature_LM75_Derived.h                 # Auxiliary SPI driver routines for ADS130B02
│                                               # simultaneous-sampling ADC
│
└── README.md                                  # Project documentation
```

> **Note on naming**: The main `.ino` sketch is named `TemperatureAlarm_copy_20260905141710.ino` and the header is `Temperature_LM75_Derived.h`. Despite their historical names from prior template code, they contain the full pedometer firmware and ADS130B02 SPI communication modules.

---

## 🧠 Step Detection Algorithm

The step detection logic operates at approximately 20 Hz (`delay(50)` loop):

1. **Total Acceleration Magnitude**:
   $$\text{acc\_total} = \sqrt{a_x^2 + a_y^2 + a_z^2}$$
2. **Noise Rejection**:
   Measurements outside the reasonable sensor window ($100 < \text{acc\_total} < 50000$) are discarded.
3. **IIR Filter**:
   $$\text{acc\_filtered} = 0.8 \times \text{acc\_filtered} + 0.2 \times \text{acc\_total}$$
4. **Z-Axis Difference**:
   $$\Delta a_z = |a_z - \text{prev\_}a_z|$$
5. **Step Identification Conditions**:
   A step is registered when all of the following conditions are met:
   - Filtered acceleration exceeds threshold: `acc_filtered > 17200.0`
   - Vertical jerk exceeds threshold: `acc_diff_z > 1000`
   - Debounce interval: at least `400 ms` has elapsed since the last detected step (`t - last_step_time > 400`)
   - `step_in_progress` flag is reset when `acc_filtered < 16400.0` or after a timeout of `1500 ms`.

---

## 🌐 Web Interface & HTTP Endpoints

The ESP32 runs an asynchronous HTTP server on port 80:

| Method | Route | Content-Type | Description |
| :--- | :--- | :--- | :--- |
| `GET` | `/` | `text/html` | Serves the responsive Dark-mode Web Dashboard with real-time stats and canvas bar chart |
| `GET` | `/data.json` | `application/json` | Returns JSON payload with total steps, cadence (pace), and 15-minute history |
| `GET` | `/counter` | `text/plain` | Returns raw total step counter |
| `POST` | `/reset` | `text/plain` | Resets the step counter and display values to zero |

### Sample `/data.json` Response

```json
{
  "total_steps": 1420,
  "pace_per_min": 78,
  "labels": ["00:00", "00:10", "00:20", ...],
  "history": [0, 12, 14, 13, 0, ...]
}
```

---

## 📦 Dependencies & Libraries

Ensure the following libraries are installed in your Arduino IDE / PlatformIO environment:

- **Built-in ESP32 Arduino Core Libraries**:
  - `WiFi`
  - `WebServer`
  - `Wire`
  - `SPI`
  - `Preferences`
- **External Arduino Libraries**:
  - [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) (`Adafruit_SSD1306.h`)
  - [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) (`Adafruit_GFX.h`)
  - [MPU6050 (I2Cdevlib / Electronic Cats)](https://github.com/ElectronicCats/mpu6050) (`MPU6050.h`)

---

## 🚀 Configuration & Setup

### 1. Configure Wi-Fi Credentials

Open `TemperatureAlarm_copy_20260905141710.ino` and update the Wi-Fi credentials around line 18:

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### 2. Connect Hardware

Wire the MPU6050 and SSD1306 OLED displays to the ESP32 according to the [Hardware Components & Pinout](#hardware-components--pinout) section.

### 3. Build and Flash

1. Open the sketch in **Arduino IDE**.
2. Select your board: **ESP32 Dev Module** (or your specific ESP32 variant).
3. Select the appropriate **COM Port**.
4. Set the upload baud rate (e.g., `921600` or `115200`).
5. Click **Upload**.

### 4. Monitor & Access Dashboard

1. Open the Serial Monitor at **115200 baud**.
2. Note the IP address printed upon connection (e.g., `IP: 192.168.1.50`).
3. The OLED will show `STATUS: Polaczono` and the initial step count.
4. Open a browser on a device connected to the same network and navigate to:
   ```text
   http://<ESP32_IP_ADDRESS>
   ```
