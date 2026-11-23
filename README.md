# ESP32-Motion-Light

ESP32-C3 PIR motion and light sensor controller with web calibrator.

## Summary

ESP32-Motion-Light is an automated room lighting system built around the ESP32-C3 SuperMini. It integrates an HW-416B PIR motion sensor and an LDR light-dependent resistor to switch an AC lamp via a relay or MOSFET switch when presence is detected in low light. The project includes the primary automated controller firmware and a standalone web-based sensor calibration tool.

## Features

- Automatic lighting activation upon motion detection under low ambient illumination.
- Web dashboard served directly from the ESP32-C3 for live status, thresholds, and manual overrides.
- Non-volatile configuration persistence using ESP32 Preferences flash storage.
- Standalone `/calibrator` firmware with live web graphs to calibrate PIR sensitivity and dusk thresholds.
- Wi-Fi Station connection with automatic SoftAP fallback mode if the primary router is unavailable.

## Hardware Requirements

- Microcontroller: ESP32-C3 SuperMini development board
- Motion Sensor: HW-416B PIR motion detector
- Ambient Light Sensor: Photoresistor (LDR) voltage divider
- Switching Element: 5V relay module or isolated MOSFET gate driver
- Indicator: Onboard status LED

## Pinout Table

| Component | ESP32-C3 Pin | Function |
| --- | --- | --- |
| LDR Analog Divider | GPIO 2 | Ambient light analog input (ADC1_CH2) |
| Relay IN / MOSFET | GPIO 3 | AC light switching control |
| PIR Motion Sensor | GPIO 4 | Digital motion trigger input |
| Status LED | GPIO 8 | Active-LOW built-in activity indicator |

## Secrets Configuration

1. Both the root project and `/calibrator` provide a `secrets.example.h` template in their respective `src/` directories.
2. Copy `secrets.example.h` to `secrets.h` and enter your Wi-Fi network SSID, password, and static IP address preferences.
3. `secrets.h` is excluded in `.gitignore` to avoid exposing network credentials.
4. If no `secrets.h` is found, the build defaults to `secrets.example.h` placeholders.

## Build and Flash Steps

### Main Controller (Root)

1. Open a terminal in the repository root directory.
2. Build the firmware using PlatformIO:
   ```bash
   pio run
   ```
3. Connect the ESP32-C3 board and flash:
   ```bash
   pio run --target upload
   ```

### Sensor Calibrator (`/calibrator`)

1. Open a terminal in the `calibrator` directory:
   ```bash
   cd calibrator
   ```
2. Build the calibrator firmware:
   ```bash
   pio run
   ```
3. Flash to the ESP32-C3:
   ```bash
   pio run --target upload
   ```
4. Open the assigned IP address in a web browser to view the real-time calibration dashboard.
