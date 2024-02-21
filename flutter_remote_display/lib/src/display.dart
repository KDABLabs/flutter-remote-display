import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'dart:ui' as ui;

import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';

abstract class DisplayConnection implements Sink<ui.Image> {}

class RGBA32ToRGB565Converter extends Converter<List<int>, Uint8List> {
  @override
  Uint8List convert(List<int> input) {
    final result = Uint8List(input.length ~/ 2);
    for (final (index, slice) in input.slices(4).indexed) {
      final [r, g, b, a] = slice;

      final (low, high) = convertSingle((r, g, b, a));

      result[index * 2] = low;
      result[index * 2 + 1] = high;
    }

    return result;
  }

  static (int, int) convertSingle((int, int, int, int) rgba) {
    final (r, g, b, _) = rgba;
    final rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    assert(rgb565 >> 16 == 0);

    return (rgb565 & 0xFF, rgb565 >> 8);
  }
}

class BluetoothDisplayConnection extends DisplayConnection {
  BluetoothDisplayConnection._(this._connection);

  final BluetoothConnection _connection;
  var _isClosed = false;

  static Future<BluetoothDisplayConnection> connect(
      String bluetoothAddress) async {
    final conn = await BluetoothConnection.toAddress(bluetoothAddress);

    debugPrint('connected to bluetooth display $bluetoothAddress: $conn');

    return BluetoothDisplayConnection._(conn);
  }

  bool get isConnected => _connection.isConnected;

  void _checkOpen() {
    if (_isClosed) {
      throw StateError('Bluetooth Display connection is already closed.');
    }
  }

  void _checkConnected() {
    if (!isConnected) {
      throw Exception('Connection to bluetooth device lost.');
    }
  }

  @protected
  Future<Uint8List> encodeImage(ui.Image image) async {
    final data =
        await image.toByteData(format: ui.ImageByteFormat.rawStraightRgba);
    if (data == null) {
      throw Exception('Image encoding failed');
    }

    final rgba32bytes =
        data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes);

    final rgb565Bytes = RGBA32ToRGB565Converter().convert(rgba32bytes);

    return rgb565Bytes;
  }

  /// Sends a frame to the target device.
  ///
  /// Callers can free the image immediately after return using
  /// [ui.Image.dispose].
  @override
  void add(ui.Image frame) async {
    _checkOpen();
    _checkConnected();

    frame = frame.clone();

    final bytes = await encodeImage(frame);

    frame.dispose();

    debugPrint(
        'sending frame: ${frame.width}x${frame.height}, ${bytes.lengthInBytes} bytes');

    _connection.output.add(bytes);
  }

  @override
  Future<void> close() async {
    _checkOpen();

    _isClosed = true;
    await _connection.close();
  }
}
