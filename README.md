# Bluetooth ESP-IDF Speaker

Native ESP-IDF project for the Freenove FNK0047 ESP32-WROVER-E and its PCM5102A audio converter/amplifier module. The ESP32 acts as a Bluetooth A2DP audio sink; received PCM audio is sent to the module over I²S.

## Status

Initial project scaffold. Build and hardware-test it before relying on it for playback. Bluetooth A2DP requires an ESP-IDF build that includes Classic Bluetooth (BR/EDR); do not select a BLE-only target/configuration.

## Hardware wiring

| FNK0047 audio module | ESP32-WROVER-E |
|---|---|
| SCK (MCLK) | GPIO 22 |
| BCK (BCLK) | GPIO 26 |
| DIN (I²S data into module) | GPIO 25 |
| LCK (LRCK/WS) | GPIO 27 |
| VCC | 3V3 |
| GND | GND |

Connect one passive speaker across **L+ and L−** (left channel) or **R+ and R−** (right channel). Do not connect either speaker terminal to GND, and do not bridge the left and right outputs. Start with low volume. Power the amplifier module within its documented supply range.

## Build in GitHub Codespaces

1. Open this repository in a Codespace.
2. Install/use Espressif's ESP-IDF extension or ESP-IDF container/toolchain.
3. In a terminal, source the ESP-IDF environment, then run:
   ```sh
   idf.py set-target esp32
   idf.py build
   ```
4. The application binary will be at `build/Bluetooth-ESP-idf.bin`; the bootloader and partition table are also generated under `build/`.

The supplied `.devcontainer` is a starting point; if the Codespace does not already have ESP-IDF installed, follow Espressif's official VS Code / ESP-IDF setup.

## Flashing

A Codespace cannot directly access a USB cable plugged into your Chromebook. Build here, then download the firmware artifacts and flash using a local computer with USB access, or configure a supported remote flashing workflow.

## Notes

- The ESP32-WROVER-E supports Classic Bluetooth A2DP; Bluetooth LE alone is not sufficient.
- The PCM5102A module receives digital audio over I²S. It is not the Bluetooth receiver itself.
- This project uses ESP-IDF C APIs, not Arduino.
