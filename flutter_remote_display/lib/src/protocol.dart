import 'dart:math';
import 'dart:typed_data';

import 'package:buffer/buffer.dart';
import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_remote_display/flutter_remote_display.dart';
import 'package:flutter_remote_display/src/encoding.dart';

enum PacketType {
  queryDeviceInfo,
  deviceInfo,
  touchEvent,
  accelerationEvent,
  physicalButtonEvent,
  backlightPacket,
  vibrationPacket,
  pingPacket,
  pongPacket,
  framePacket,
}

abstract class Packet implements ByteSerializable {
  PacketType get type;

  void writePacketBody(ByteDataWriter writer);

  @override
  void write(ByteDataWriter writer) {
    writer.writeUint8(type.index);
    writePacketBody(writer);
  }

  Uint8List toBytes() {
    final writer = ByteDataWriter(endian: Endian.little);
    write(writer);
    return writer.toBytes();
  }

  int getLength() {
    final writer = DiscardingByteDataWriter(endian: Endian.little);
    write(writer);
    return writer.length;
  }

  static double calculateCompressionRatio(
    Packet compressed,
    Packet raw,
  ) {
    return compressed.getLength() / raw.getLength();
  }
}

abstract class DisplayToHostPacket extends Packet {
  static DisplayToHostPacket readPacket(ByteDataReader reader) {
    final typeIndex = reader.readUint8();
    final type = PacketType.values[typeIndex];

    switch (type) {
      case PacketType.touchEvent:
        return TouchEvent.readPacketBody(reader);
      case PacketType.accelerationEvent:
        return AccelerationEvent.readPacketBody(reader);
      case PacketType.physicalButtonEvent:
        return PhysicalButtonEvent.readPacketBody(reader);
      case PacketType.pongPacket:
        return PongPacket.readPacketBody(reader);
      default:
        throw Exception('Unknown packet type: $type');
    }
  }
}

enum TouchEventPhase { down, move, up }

class TouchEvent extends DisplayToHostPacket {
  TouchEvent({
    required this.pointer,
    required this.phase,
    required this.timestamp,
    required this.position,
  });

  final int pointer;
  final int timestamp;
  final TouchEventPhase phase;
  final (int, int) position;

  @override
  final type = PacketType.touchEvent;

  @override
  void writePacketBody(ByteDataWriter writer) {
    writer.writeUint8(pointer);

    writer.writeUint32(timestamp);

    writer.writeUint8(phase.index);

    final (x, y) = switch (position) {
      (int, int) p => p,
      null => (0, 0),
    };
    writer.writeUint8(x);
    writer.writeUint8(y);
  }

  static TouchEvent readPacketBody(ByteDataReader reader) {
    final pointer = reader.readUint8();
    final timestamp = reader.readUint32();
    final phaseIndex = reader.readUint8();
    final phase = TouchEventPhase.values[phaseIndex];
    final x = reader.readUint8();
    final y = reader.readUint8();

    return TouchEvent(
      pointer: pointer,
      timestamp: timestamp,
      phase: phase,
      position: (x, y),
    );
  }
}

enum AccelerationEventKind { step, wake }

class AccelerationEvent extends DisplayToHostPacket
    implements ByteSerializable {
  AccelerationEvent(this.kind);

  @override
  final type = PacketType.accelerationEvent;

  final AccelerationEventKind kind;

  @override
  void writePacketBody(ByteDataWriter writer) {
    writer.writeUint8(kind.index);
  }

  static AccelerationEvent readPacketBody(ByteDataReader reader) {
    final kindIndex = reader.readUint8();
    final kind = AccelerationEventKind.values[kindIndex];
    return AccelerationEvent(kind);
  }
}

class PhysicalButtonEvent extends DisplayToHostPacket
    implements ByteSerializable {
  PhysicalButtonEvent(this.button);

  @override
  final type = PacketType.physicalButtonEvent;

  final int button;

  @override
  void writePacketBody(ByteDataWriter writer) {
    writer.writeUint8(button);
  }

  static PhysicalButtonEvent readPacketBody(ByteDataReader reader) {
    final button = reader.readUint8();
    return PhysicalButtonEvent(button);
  }
}

abstract class HostToDisplayPacket extends Packet {}

class BacklightPacket extends HostToDisplayPacket {
  BacklightPacket(this.intensity) : assert(intensity >= 0 && intensity <= 1);

  final double intensity;

  @override
  final type = PacketType.backlightPacket;

  @override
  void writePacketBody(ByteDataWriter writer) {
    final byte = (intensity * 255).round();
    writer.writeUint8(byte);
  }

  static BacklightPacket readPacketBody(ByteDataReader reader) {
    final byte = reader.readUint8();
    return BacklightPacket(byte / 255);
  }
}

class VibrationPacket extends HostToDisplayPacket {
  VibrationPacket(this.duration);

  final Duration duration;

  @override
  final type = PacketType.vibrationPacket;

  @override
  void writePacketBody(ByteDataWriter writer) {
    var centiSeconds = duration.inMilliseconds ~/ 10;
    if (centiSeconds > 255) {
      centiSeconds = 255;
    }

    writer.writeUint8(centiSeconds);
  }

  static VibrationPacket readPacketBody(ByteDataReader reader) {
    final centiSeconds = reader.readUint8();
    return VibrationPacket(Duration(milliseconds: centiSeconds * 10));
  }
}

class PingPacket extends HostToDisplayPacket {
  @override
  final type = PacketType.pingPacket;

  @override
  void writePacketBody(ByteDataWriter writer) {}

  static PingPacket readPacketBody(ByteDataReader reader) {
    return PingPacket();
  }
}

class PongPacket extends DisplayToHostPacket {
  @override
  final type = PacketType.pongPacket;

  @override
  void writePacketBody(ByteDataWriter writer) {}

  static PongPacket readPacketBody(ByteDataReader reader) {
    return PongPacket();
  }
}

enum FrameEncoding { rawKeyframe, rleKeyframe, rawDeltaframe, rleDeltaframe }

abstract class FramePacket extends HostToDisplayPacket {
  @override
  final type = PacketType.framePacket;

  FrameEncoding get encoding;

  void writeFrameBody(ByteDataWriter writer);

  @override
  void writePacketBody(ByteDataWriter writer) {
    writer.writeUint8(encoding.index);

    writeFrameBody(writer);
  }

  static FramePacket? build(
    ImageData image, {
    ImageData? old,
    PixelFormat? pixelFormat,
  }) {
    if (old == null) {
      return RLEKeyFramePacket.build(image, format: pixelFormat);
    } else {
      return RLEDeltaFramePacket.build(
        image,
        oldImage: old,
        pixelFormat: pixelFormat,
      );
    }
  }
}

class RawDamageRect implements ByteSerializable {
  RawDamageRect(this.rect, this.bytes);

  final IntRect rect;
  final Uint8List bytes;

  @override
  void write(ByteDataWriter writer) {
    writer.writeUint8(rect.left);
    writer.writeUint8(rect.top);
    writer.writeUint8(rect.width);
    writer.writeUint8(rect.height);

    writer.write(bytes);
  }
}

class RLEDamageRect implements ByteSerializable {
  RLEDamageRect(this.rect, this.runs);

  final IntRect rect;
  final Iterable<(int, int)> runs;

  @override
  void write(ByteDataWriter writer) {
    writer.writeUint8(rect.left);
    writer.writeUint8(rect.top);
    writer.writeUint8(rect.width);
    writer.writeUint8(rect.height);

    writer.writeUint16(runs.length);

    for (final (length, rgb565) in runs) {
      writer.writeUint8(length);
      writer.writeUint16(rgb565);
    }
  }
}

mixin RLEFrame {
  PixelFormat get pixelFormat;

  static Iterable<(int, int)> buildRunsForRow(Iterable<int> pixels) {
    final runs = <(int, int)>[];

    var run = (1, pixels.first);

    final it = pixels.skip(1).iterator;
    while (it.moveNext()) {
      final next = it.current;

      if (next != run.$2) {
        runs.add(run);

        run = (0, next);
      }

      run = (run.$1 + 1, run.$2);
    }

    runs.add(run);

    return runs;
  }

  static Iterable<(int, int)> buildRuns(
    ImageData image, {
    PixelFormat? format,
  }) {
    if (format != null) {
      image = image.convert(format);
    }

    var runs = buildRunsForRow(image.getRow(0));

    for (var y = 1; y < image.height; y++) {
      runs = runs.followedBy(buildRunsForRow(image.getRow(y)));
    }

    return runs;
  }

  void writeRLERuns(ByteDataWriter writer, Iterable<(int, int)> runs) {
    writer.writeUint16(runs.length);

    for (final run in runs) {
      final (runLength, color) = run;
      writer.writeUint8(runLength);

      assert(pixelFormat.bpp == 2);
      writer.writeUint16(color);
    }
  }
}

mixin DeltaFrame {
  static Iterable<IntRect> findDamagedRects({
    required ImageData oldImage,
    required ImageData newImage,
  }) {
    // assert image dimensions and bpp are equal
    assert(oldImage.width == newImage.width);
    assert(oldImage.height == newImage.height);
    assert(oldImage.bpp == newImage.bpp);

    final width = oldImage.width;
    final height = oldImage.height;
    final bpp = oldImage.bpp;

    var rect = IntRect.zero;
    for (var i = 0; i < oldImage.stride * height; i++) {
      final oldByte = oldImage.bytes[i];
      final newByte = newImage.bytes[i];

      if (oldByte != newByte) {
        final (x, y) = oldImage.getCoordinatesForOffset(i);
        final pixelRect = IntRect.fromLTWH(x, y, 1, 1);
        rect = rect.expandToInclude(pixelRect);
      }
    }

    if (rect.isEmpty) {
      return [];
    } else {
      return [rect];
    }
  }
}

class RawKeyFramePacket extends FramePacket {
  RawKeyFramePacket(this.bytes);

  final Uint8List bytes;

  @override
  final encoding = FrameEncoding.rawKeyframe;

  @override
  void writeFrameBody(ByteDataWriter writer) {
    writer.write(bytes);
  }
}

class RLEKeyFramePacket extends FramePacket with RLEFrame {
  RLEKeyFramePacket(this.runs, {required this.pixelFormat});

  final Iterable<(int, int)> runs;

  @override
  final PixelFormat pixelFormat;

  @override
  final encoding = FrameEncoding.rleKeyframe;

  @override
  void writeFrameBody(ByteDataWriter writer) {
    writeRLERuns(writer, runs);
  }

  static RLEKeyFramePacket build(ImageData image, {PixelFormat? format}) {
    return RLEKeyFramePacket(
      RLEFrame.buildRuns(image, format: format),
      pixelFormat: format ?? image.format,
    );
  }
}

class RawDeltaFramePacket extends FramePacket with DeltaFrame {
  RawDeltaFramePacket(this.bytes);

  final Uint8List bytes;

  @override
  final encoding = FrameEncoding.rawDeltaframe;

  @override
  void writeFrameBody(ByteDataWriter writer) {
    writer.write(bytes);
  }

  static RawDeltaFramePacket? build(Uint8List imageARGBBytes) {
    throw UnimplementedError();
  }
}

class RLEDeltaFramePacket extends FramePacket with DeltaFrame, RLEFrame {
  RLEDeltaFramePacket(this.rects, {required this.pixelFormat});

  final Iterable<RLEDamageRect> rects;

  @override
  final encoding = FrameEncoding.rleDeltaframe;

  final PixelFormat pixelFormat;

  @override
  void writeFrameBody(ByteDataWriter writer) {
    writer.writeUint16(rects.length);

    for (final tile in rects) {
      tile.write(writer);
    }
  }

  static RLEDeltaFramePacket? build(
    ImageData image, {
    ImageData? oldImage,
    Iterable<IntRect>? damagedRects,
    PixelFormat? pixelFormat,
  }) {
    // assert old image or dirty rects are provided
    assert(oldImage != null || damagedRects != null);

    if (pixelFormat != null) {
      image = image.convert(pixelFormat);
    }

    if (oldImage != null &&
        (pixelFormat != null || image.format != oldImage.format)) {
      oldImage = oldImage.convert(pixelFormat ?? image.format);
    }

    final rects = <RLEDamageRect>[];

    damagedRects ??= DeltaFrame.findDamagedRects(
      oldImage: oldImage!,
      newImage: image,
    );

    assert((() {
      damagedRects!;

      // no rects overlap
      for (final (i, a) in damagedRects.indexed) {
        final rectsExceptA =
            damagedRects.whereNotIndexed((index, _) => index == i);

        for (final b in rectsExceptA) {
          if (a.overlaps(b)) {
            return false;
          }
        }
      }

      return true;
    })());

    for (final rect in damagedRects) {
      final damagedImage = image.view(rect);
      final runs = RLEFrame.buildRuns(damagedImage);

      rects.add(RLEDamageRect(rect, runs));
    }

    return rects.isNotEmpty
        ? RLEDeltaFramePacket(rects, pixelFormat: pixelFormat ?? image.format)
        : null;
  }
}
