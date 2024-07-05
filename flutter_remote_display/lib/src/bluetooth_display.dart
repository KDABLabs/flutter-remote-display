import 'dart:async';
import 'dart:typed_data';

import 'dart:ui' as ui;

import 'package:buffer/buffer.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';
import 'package:flutter_remote_display/flutter_remote_display.dart';
import 'package:flutter_remote_display/src/protocol.dart';

class BluetoothDisplayConnection extends DisplayConnection {
  BluetoothDisplayConnection._(this._connection)
      : _inputController = StreamController.broadcast() {
    _connectionSub = _connection.input!.listen((data) {
      final reader = ByteDataReader(endian: Endian.little);
      reader.add(data);

      while (reader.remainingLength > 0) {
        /// FIXME: If the packet needs more data than we have available,
        /// we should wait for more data to arrive before trying to read
        /// the packet.
        final packet = DisplayToHostPacket.readPacket(reader);
        _inputController.add(packet);
      }
    });
  }

  final BluetoothConnection _connection;
  late final StreamSubscription _connectionSub;
  final StreamController<DisplayToHostPacket> _inputController;

  var _isClosed = false;
  ImageData? _previousImageData;

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

  Future<ImageData> _getImageData(ui.Image image) async {
    return (await ImageData.fromDartUIImage(image)).toRGB565();
  }

  /// Sends a frame to the target device.
  ///
  /// Callers can free the image immediately after return using
  /// [ui.Image.dispose].
  @override
  Future<void> addFrame(ui.Image frame) async {
    _checkOpen();
    _checkConnected();

    frame = frame.clone();

    var imageData = await _getImageData(frame);
    _checkConnected();

    frame.dispose();

    imageData = imageData.convert(PixelFormat.rgb565);

    final packet = FramePacket.build(
      imageData,
      old: _previousImageData,
      pixelFormat: PixelFormat.rgb565,
    );

    if (packet != null) {
      final future = addPacket(packet);
      _previousImageData = imageData;
      return await future;
    }
  }

  Future<void> addPacket(HostToDisplayPacket packet) async {
    _checkOpen();
    _checkConnected();

    final bytes = packet.toBytes();

    _connection.output.add(bytes);

    return await _connection.output.allSent;
  }

  @override
  Future<void> close() async {
    _checkOpen();

    _isClosed = true;
    await _connection.close();
  }

  @override
  Stream<DisplayToHostPacket> get input => _inputController.stream;

  @override
  Sink<HostToDisplayPacket> get output => throw UnimplementedError();
}
