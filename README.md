# esPod

An ESP32-based Bluetooth adapter that emulates an Apple iPod for Audi MMI 3G systems.

Unlike a generic A2DP receiver, this project is focused on seamless integration with the original Audi MMI 3G user interface. The goal is to make Bluetooth audio behave as closely as possible to a real iPod connected through the AMI interface.

## Features

* Bluetooth A2DP audio streaming, with two interchangeable backends:
  * the [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) library (Arduino)
  * a native ESP-IDF/Bluedroid backend, for use with a patched ESP-IDF build that adds AAC/aptX codec support
* AVRCP playback control, with automatic reconnection to the last known device
* Track, artist, album and playback status synchronization
* iPod Accessory Protocol (iAP) emulation
* Optimized for Audi MMI 3G
* Playlist navigation with **Previous / Current / Next** track support
* Improved metadata synchronization for reliable track change notifications
* Audio output automatically reconfigures to the negotiated sample rate/bit depth/channel count, rather than assuming fixed CD-quality SBC
* Persistent, runtime-configurable settings (seek-as-volume, peer-name usage, track-position and zero-volume workarounds, suspend timeout, ...) stored in NVS — no longer build-time-only `#define`s
* `Disabled` / `Suspended` / `Enabled` connection state machine: a brief Bluetooth dropout no longer causes the MMI to switch away from the active source — DCD stays asserted and UART keeps responding until a configurable grace period elapses
* Platform abstraction layer to simplify migration from Arduino to ESP-IDF

## Project goals

The primary objective of this project is not simply Bluetooth audio playback.

The focus is to provide an experience that is as close as possible to using a genuine iPod with Audi MMI 3G.

In particular, the project aims to:

* display complete metadata in the MMI interface;
* provide smooth playback control from the vehicle;
* maintain proper synchronization between Bluetooth playback and iAP state;
* support the Audi MMI 3G playlist model consisting of:

  * Previous track
  * Current track
  * Next track
* keep the implementation independent of a specific Bluetooth stack whenever possible.

## Architecture

The project is being gradually refactored into independent modules, each exposed to the iAP core through a small interface rather than a concrete implementation:

* **UART interface** (`IUart`) — `ArduinoUart` / `EspIdfUart`
* **Platform abstraction** (`platform::` namespace) — time (`time_now_ms`/`delay_ms`) and GPIO (`gpio_configure`/`gpio_write`/`gpio_read`), each independently switchable between the Arduino and ESP-IDF implementation
* **Bluetooth backend** (`IBluetoothPlaybackSource` / `IBluetoothSourceEvents`) — outgoing playback commands and incoming connection/metadata/position events, decoupled from any specific Bluetooth stack:
  * `Esp32A2dpBluetoothSource`, backed by the ESP32-A2DP library
  * `NativeA2dpBluetoothSource`, backed by a native ESP-IDF/Bluedroid wrapper (`NativeA2dpBluetoothSink`)
* **Audio output** (`IAudioOutput`) — where decoded PCM actually goes, reconfigurable on the fly as the negotiated codec format changes:
  * `Esp32I2sAudioOutput` (AudioTools `I2SStream`)
  * `NativeI2sAudioOutput` (ESP-IDF `i2s_std` driver)
* **Persistent settings** (`storage::` namespace) — get/set bool/int/string values backed by NVS on both Arduino (`ArduinoNvs`) and native (`nvs_*` APIs); each component (`esPod`, Bluetooth backend, audio output) loads its own settings via an explicit `loadSettingsFromStorage()` rather than reading storage from a constructor, since globals are constructed before NVS is ready

The iAP core (`esPod` and the `L0x0x` command handlers) talks only to these interfaces and has no knowledge of Arduino, a specific Bluetooth library, or a specific I2S driver.

## Project status

The project is under active development. Both Bluetooth backends are functional, including automatic reconnection; the native ESP-IDF backend is the current focus, since it is what unlocks the patched-codec (AAC/aptX) build.

Current work is mainly focused on:

* validating the native ESP-IDF backend on real hardware (reconnect behavior, metadata reliability, audio output across different codecs);
* removing remaining Arduino framework dependencies from the iAP core and shared utility code;
* migrating off deprecated ESP-IDF APIs (legacy I2S driver already replaced with `i2s_std`);
* unifying code style across the codebase;
* improving AVRCP metadata handling;
* a web-based configuration UI, now that settings are persisted at runtime (`storage::`) rather than build-time `#define`s.

## Hardware

The reference build is a generic **ESP32 NodeMCU** dev board plus a **UDA1334A** I2S DAC breakout, wired directly into an Audi MMI 3G's AMI connectors (12-pin "C" connector and 4-pin USB connector) - no level shifter is needed, since the MMI's iPod/AMI lines are already 3.3V logic.

The pins below match the defaults in `main.cpp` for this build (the non-AUDIOKIT / "Sandwich Carrier Board" path); an alternate AudioKit-board build path also currently exists in the code but isn't the focus going forward.

### ESP32 ↔ Audi MMI 3G

| ESP32 | MMI connector, pin | Signal | Notes |
|---|---|---|---|
| GPIO16 (UART RX) | 12-pin "C", pin 12 | iPod data TX (MMI → ESP32) | |
| GPIO17 (UART TX) | 12-pin "C", pin 11 | iPod data RX (ESP32 → MMI) | |
| GPIO5 | 4-pin USB, pin 2 | iPod detected (DCD) | only wired/driven when built with `ENABLE_ACTIVE_DCD`; reflects esPod's `Disabled`/`Suspended`/`Enabled` state |
| 5V | 12-pin "C", pin 3 | USB +5V | powers the ESP32 |
| GND | 12-pin "C", pin 4 | USB ground | |
| — | 12-pin "C", pin 6 | Detect, 1kΩ to ground | passive strap, **not** wired to the ESP32 - just tells the MMI an AMI accessory is present, independent of the GPIO5 DCD line above |
| — | 12-pin "C", pin 8 | Audio shield | tie to chassis/ground if desired; not required |
| — | 4-pin USB, pins 1/3/4 (D+/D−/Earth) | — | not used by esPod - only relevant if you also pass a USB flash drive through to the MMI, unrelated to this project |

### ESP32 ↔ UDA1334A (I2S audio out)

The UDA1334A's analog output feeds the MMI's own audio-in pins on the same 12-pin connector, the same way a real iPod would.

| ESP32 | UDA1334A | Notes |
|---|---|---|
| GPIO27 | BCLK | |
| GPIO25 | WSEL (LRCLK) | |
| GPIO26 | DIN | |
| 3V3 | VIN | |
| GND | GND | UDA1334A generates its own system clock - no MCLK pin to wire |

| UDA1334A | MMI connector, pin | Signal |
|---|---|---|
| LOUT | 12-pin "C", pin 7 | Audio left |
| ROUT | 12-pin "C", pin 2 | Audio right |
| AGND | 12-pin "C", pin 1 | Audio ground |

Onboard LED (`LED_BUILTIN`, GPIO2) needs no external wiring.

## Build

The project can be built either as a plain Arduino-framework PlatformIO project (`framework = arduino`, ESP32-A2DP backend), or as a native ESP-IDF project (`framework = espidf`, via `idf.py` or PlatformIO) against a patched ESP-IDF providing AAC/aptX codec support. A hybrid `framework = arduino, espidf` build was explored but is not currently used. See `platformio.ini` / `sdkconfig` for the current build configuration.

## Origin

This project originated as a fork of **martinroger/ipodesp32**.

The original source tree was cleaned up and the project has since evolved independently with a focus on Audi MMI 3G compatibility, modular architecture, and platform abstraction.
