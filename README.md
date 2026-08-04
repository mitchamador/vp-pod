# esPod

An ESP32-based Bluetooth adapter that emulates an Apple iPod for Audi MMI 3G systems.

Unlike a generic A2DP receiver, this project is focused on seamless integration with the original Audi MMI 3G user interface. The goal is to make Bluetooth audio behave as closely as possible to a real iPod connected through the AMI interface.

## Features

* Bluetooth A2DP audio streaming
* AVRCP playback control
* Track, artist, album and playback status synchronization
* iPod Accessory Protocol (iAP) emulation
* Optimized for Audi MMI 3G
* Playlist navigation with **Previous / Current / Next** track support
* Improved metadata synchronization for reliable track change notifications
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

The project is being gradually refactored into independent modules.

Current abstraction layers include:

* UART interface
* Platform time abstraction
* Bluetooth manager
* Platform-specific implementations

The long-term objective is to isolate the core iAP implementation from platform-specific code, making it possible to support multiple backends such as:

* Arduino Framework
* ESP-IDF
* External Bluetooth modules connected over UART

## Project status

The project is under active development.

Current work is mainly focused on:

* removing Arduino framework dependencies;
* improving modularity;
* separating platform-specific code from the iAP core;
* improving AVRCP metadata handling;
* preparing for native ESP-IDF support.

## Origin

This project originated as a fork of **martinroger/ipodesp32**.

The original source tree was cleaned up and the project has since evolved independently with a focus on Audi MMI 3G compatibility, modular architecture, and platform abstraction.
