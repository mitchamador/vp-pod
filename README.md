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

The iAP core (`esPod` and the `L0x0x` command handlers) talks only to these interfaces and has no knowledge of Arduino, a specific Bluetooth library, or a specific I2S driver.

## Project status

The project is under active development. Both Bluetooth backends are functional, including automatic reconnection; the native ESP-IDF backend is the current focus, since it is what unlocks the patched-codec (AAC/aptX) build.

Current work is mainly focused on:

* validating the native ESP-IDF backend on real hardware (reconnect behavior, metadata reliability, audio output across different codecs);
* removing remaining Arduino framework dependencies from the iAP core and shared utility code;
* migrating off deprecated ESP-IDF APIs (legacy I2S driver already replaced with `i2s_std`);
* unifying code style across the codebase;
* improving AVRCP metadata handling;
* longer-term: a platform-independent settings-storage layer, and runtime (web-based) configuration in place of today's build-time `#define`s.

## Build

The project can be built either as a plain Arduino-framework PlatformIO project (`framework = arduino`, ESP32-A2DP backend), or as a native ESP-IDF project (`framework = espidf`, via `idf.py` or PlatformIO) against a patched ESP-IDF providing AAC/aptX codec support. A hybrid `framework = arduino, espidf` build was explored but is not currently used. See `platformio.ini` / `sdkconfig` for the current build configuration.

## Origin

This project originated as a fork of **martinroger/ipodesp32**.

The original source tree was cleaned up and the project has since evolved independently with a focus on Audi MMI 3G compatibility, modular architecture, and platform abstraction.
