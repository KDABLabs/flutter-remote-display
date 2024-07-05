flutter_remote_display is a package and protocol to use a remote (bluetooth-connected) MCU as an "external" flutter display.

DISCLAIMER: This is a very early prototype / proof of concept. Expect bugs and suboptimal performance.

## Features

- Connect to remote MCU, running the flutter-remote-display display code, using bluetooth (classic)
- The basic remote-display protocol on the MCU is implemented in an MCU-agnostic way, it just depends on FreeRTOS.
  So you can just depend on it and use it's interfaces to make your custom MCU and board work.
- Currently supported devices in upstream are:
  - [TTGO T-Watch 2020 V1 and V3](https://www.lilygo.cc/products/t-watch-2020-v3)
- flutter-remote-display currently uses Bluetooth Classic and the bluetooth SPP (Serial Port Profile).
  for that, it uses flutter_bluetooth_serial, which is Android-only
- for that reason, flutter_remote_display is **Android-only** right now as well.

## Getting started

This project consists of multiple parts, and the flutter_remote_display package is just the flutter part. The instructions on how to use it all are in the monorepo: https://github.com/KDABLabs/flutter-remote-display.git

## Usage

See the example at: https://github.com/KDABLabs/flutter-remote-display/blob/main/flutterino_flutter/lib/main.dart

