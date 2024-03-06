# flutter-remote-display

Flutter package for rendering content on remote (e.g. WiFi- or Bluetooth-connected) displays.

(Right now, also contains the example implementation in `flutterino_flutter` and `flutterino_esp32`)

## Structure

1. `flutter_remote_display`
    - implements the remote-display protocol
    - currently only Bluetooth (Classic, Serial Port Profile), with `flutter_bluetooth_serial` package
    - `flutter_bluetooth_serial` is Android only
    - contains `RemoteView` widget that renders contents to a remote display
2. `flutterino_flutter`
    - example app using `flutter_remote_display`, flutter part
    - currently contains hardcoded values for the remote device bluetooth address
    - establishes the bluetooth connection, requests permissions
3. `flutterino_esp32`
    - ESP32 part for `flutterino_flutter` as an ESP-IDF project
    - Currently assumes a TTGO T-Watch 2020 V1
    - some pins need to be updated for different versions of the watch,
      or different ESP32 boards
    - **For Development**:
        - Install ESP-IDF VS Code extension
        - Use the extensions guide to install ESP-IDF v5.1 (_not_ 5.2, that's not supported by the arduino library)
        - You should now be able to build & flash the project to an attached board, using the controls in the bottom bar.
