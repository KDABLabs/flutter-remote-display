import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';

import 'dart:ui' as ui;

import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';

abstract class DisplayConnection implements Sink<ui.Image> {}

extension Uint8SubListView on Uint8List {
  Uint8List sublistView(int start, [int? end]) {
    end ??= length;

    return buffer.asUint8List(
      offsetInBytes + start * elementSizeInBytes,
      (end - start) * elementSizeInBytes,
    );
  }
}

class RGBA32ToRGB565Converter extends Converter<List<int>, Uint8List> {
  @override
  Uint8List convert(List<int> input, {Uint8List? into}) {
    final Uint8List result;
    if (into == null) {
      result = Uint8List(input.length ~/ 2);
    } else {
      result = into;
    }

    for (final (index, slice) in input.slices(4).indexed) {
      final [r, g, b, a] = slice;

      // final (low, high) = convertSingle((r, g, b, a));
      final (low, high) = convertSingle((b, g, r, a));

      result[index * 2] = high;
      result[index * 2 + 1] = low;
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

class RemoteDisplayException implements Exception {
  RemoteDisplayException(this.message);

  RemoteDisplayException.connectionLost() : message = 'Connection lost';

  final String message;

  @override
  String toString() {
    return 'Remote Display Exception: $message';
  }
}

class BluetoothDisplayConnection extends DisplayConnection {
  BluetoothDisplayConnection._(this._connection);

  final BluetoothConnection _connection;
  var _isClosed = false;
  Uint8List? _previousRgb565Bytes;

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
      throw RemoteDisplayException.connectionLost();
    }
  }

  @protected
  Future<Uint8List?> encodeImage(ui.Image image) async {
    final data =
        await image.toByteData(format: ui.ImageByteFormat.rawStraightRgba);
    if (data == null) {
      throw Exception('Image encoding failed');
    }

    final rgba32bytes =
        data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes);

    final rgb565Bytes = RGBA32ToRGB565Converter().convert(rgba32bytes);

    // xor the previous frame with the current frame to get the delta
    final Rect dirtyRect;
    if (_previousRgb565Bytes != null) {
      // for now just have one dirty rect at max
      Rect? rect;
      for (var i = 0; i < rgb565Bytes.length; i += 2) {
        final low = rgb565Bytes[i] ^ _previousRgb565Bytes![i];
        final high = rgb565Bytes[i + 1] ^ _previousRgb565Bytes![i + 1];

        if (low != 0 || high != 0) {
          final x = i ~/ 2 % image.width;
          final y = i ~/ 2 ~/ image.width;

          final pixel = Rect.fromLTWH(x.toDouble(), y.toDouble(), 1, 1);
          if (rect == null) {
            rect = pixel;
          } else {
            rect = rect.expandToInclude(pixel);
          }
        }
      }

      dirtyRect = rect ?? Rect.zero;
    } else {
      dirtyRect = Rect.fromLTWH(
        0,
        0,
        image.width.toDouble(),
        image.height.toDouble(),
      );
    }

    _previousRgb565Bytes = rgb565Bytes;

    debugPrint('dirty rect: $dirtyRect');

    final x = dirtyRect.left.toInt();
    final y = dirtyRect.top.toInt();
    final w = dirtyRect.width.toInt();
    final h = dirtyRect.height.toInt();

    // if the dirty rect is empty, the pixel contents didn't change
    if (w == 0 && h == 0) {
      return null;
    }

    final packet = Uint8List(4 + w * h * 2);
    packet[0] = x;
    packet[1] = y;
    packet[2] = w;
    packet[3] = h;

    final packetRgb565Bytes = packet.sublistView(4);
    for (var yCursor = y; yCursor < y + h; yCursor++) {
      List.copyRange(
        packetRgb565Bytes,
        (yCursor - y) * w * 2,
        rgb565Bytes,
        (yCursor * image.width + x) * 2,
        (yCursor * image.width + x + w) * 2,
      );
    }

    return packet;
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
    _checkConnected();

    frame.dispose();

    if (bytes != null) {
      debugPrint('sending frame: ${bytes.length} bytes');

      _connection.output.add(bytes);
    } else {
      debugPrint('frame didn\'t change, not sending');
    }
  }

  @override
  Future<void> close() async {
    _checkOpen();

    _isClosed = true;
    await _connection.close();
  }
}
