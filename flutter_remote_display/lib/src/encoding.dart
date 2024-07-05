import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:buffer/buffer.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

extension Uint8SubListView on Uint8List {
  Uint8List sublistView(int start, [int? end]) {
    end ??= length;

    return buffer.asUint8List(
      offsetInBytes + start * elementSizeInBytes,
      (end - start) * elementSizeInBytes,
    );
  }
}

extension ByteIterableToInt on Iterable<int> {
  int toInt({Endian endian = Endian.little, int? length}) {
    length ??= this.length;

    var result = 0;
    final it = iterator;
    switch (endian) {
      case Endian.big:
        for (var i = length; i > 0; i--) {
          result = (result << 8) | ((it.moveNext() ? it.current : 0));
        }

      case Endian.little:
        for (var i = 0; i < length; i++) {
          result = result | (it.moveNext() ? it.current << (i * 8) : 0);
        }
    }

    return result;
  }
}

extension ByteListToInt on List<int> {
  int toInt({Endian endian = Endian.little, int? length, int offset = 0}) {
    length ??= this.length;

    var result = 0;
    switch (endian) {
      case Endian.big:
        for (var i = offset; i < offset + length; i++) {
          result = (result << 8) | this[i];
        }

      case Endian.little:
        for (var i = offset + length - 1; i >= offset; i--) {
          result = (result << 8) | this[i];
        }
    }

    return result;
  }
}

extension IntToBytes on int {
  Uint8List toBytes(
    int length, {
    Endian endian = Endian.little,
    Uint8List? into,
    int? offset,
  }) {
    into ??= Uint8List(length);

    if (into.length < length) {
      throw ArgumentError.value(into, 'into');
    }

    for (var i = 0; i < length; i++) {
      final byte = switch (endian) {
        Endian.little => (this >> (i * 8)) & 0xFF,
        Endian.big => (this >> ((length - 1 - i) * 8)) & 0xFF,
        _ => throw ArgumentError.value(endian, 'endian'),
      };

      into[i + (offset ?? 0)] = byte;
    }

    return into;
  }
}

abstract class PixelFormatConverter extends Converter<List<int>, Uint8List> {
  PixelFormatConverter.construct();

  factory PixelFormatConverter(
    PixelFormat source,
    PixelFormat dest,
  ) {
    if (source == PixelFormat.rgba8888 && dest == PixelFormat.rgb565) {
      return RGBA32ToRGB565PixelFormatConverter();
    }

    return UniversalPixelFormatConverter(source: source, dest: dest);
  }

  PixelFormat get source;
  PixelFormat get dest;
}

class UniversalPixelFormatConverter extends PixelFormatConverter {
  UniversalPixelFormatConverter({
    required this.source,
    required this.dest,
  }) : super.construct();

  @override
  final PixelFormat source;

  @override
  final PixelFormat dest;

  @override
  Uint8List convert(List<int> input, {Uint8List? into}) {
    if (input.length % source.bpp != 0) {
      throw ArgumentError.value(
        input,
        'input',
        'input length must be a multiple of source.bpp',
      );
    }

    final nPixels = input.length ~/ source.bpp;

    final Uint8List result;
    if (into == null) {
      result = Uint8List(nPixels * dest.bpp);
    } else {
      if (into.length < nPixels * dest.bpp) {
        throw ArgumentError.value(
          into,
          'into',
          'into must have enough space to store the complete converted bytes (input.length ~/ source.bpp * dest.bpp)',
        );
      }

      result = into;
    }

    for (var index = 0; index < nPixels; index++) {
      convertSingle(
        input,
        sourceOffset: index * source.bpp,
        into: result,
        intoOffset: index * dest.bpp,
      );
    }

    return result;
  }

  Uint8List convertSingle(
    List<int> bytes, {
    int sourceOffset = 0,
    Uint8List? into,
    int? intoOffset,
  }) {
    if (into == null) {
      into = Uint8List(dest.bpp);
    } else {
      if (into.length - intoOffset! < dest.bpp) {
        throw ArgumentError.value(
          into,
          'into',
          'into must have space for at least dest.bpp bytes',
        );
      }
    }

    final value = PixelFormat.convert(
      bytes.toInt(length: source.bpp, offset: sourceOffset),
      sourceFormat: source,
      destFormat: dest,
    );

    return value.toBytes(dest.bpp, into: into, offset: intoOffset);
  }
}

class RGBA32ToRGB565PixelFormatConverter extends PixelFormatConverter {
  RGBA32ToRGB565PixelFormatConverter() : super.construct();

  @override
  final PixelFormat source = PixelFormat.rgba8888;

  @override
  final PixelFormat dest = PixelFormat.rgb565;

  @override
  Uint8List convert(List<int> input, {Uint8List? into}) {
    if (input.length % source.bpp != 0) {
      throw ArgumentError.value(
        input,
        'input',
        'input length must be a multiple of source.bpp',
      );
    }

    final nPixels = input.length ~/ source.bpp;

    final Uint8List result;
    if (into == null) {
      result = Uint8List(nPixels * dest.bpp);
    } else {
      if (into.length < nPixels * dest.bpp) {
        throw ArgumentError.value(
          into,
          'into',
          'into must have enough space to store the complete converted bytes (input.length ~/ source.bpp * dest.bpp)',
        );
      }

      result = into;
    }

    for (var index = 0; index < nPixels; index++) {
      final r = input[index * source.bpp];
      final g = input[index * source.bpp + 1];
      final b = input[index * source.bpp + 2];

      final value = (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11);

      final high = (value >> 8) & 0xFF;
      final low = value & 0xFF;

      result[index * dest.bpp] = low;
      result[index * dest.bpp + 1] = high;
    }

    return result;
  }
}

enum PixelFormat {
  bgr565.opaque(
    2,
    redShift: 11,
    redBits: 5,
    greenShift: 5,
    greenBits: 6,
    blueShift: 0,
    blueBits: 5,
  ),
  rgb565.opaque(
    2,
    redShift: 0,
    redBits: 5,
    greenShift: 5,
    greenBits: 6,
    blueShift: 11,
    blueBits: 5,
  ),
  rgb565BigEndian.opaque(
    2,
    redShift: 0,
    redBits: 5,
    greenShift: 5,
    greenBits: 6,
    blueShift: 11,
    blueBits: 5,
    endian: Endian.big,
  ),
  rgba8888(
    4,
    redShift: 0,
    redBits: 8,
    greenBits: 8,
    greenShift: 8,
    blueShift: 16,
    blueBits: 8,
    alphaShift: 24,
    alphaBits: 8,
  ),
  bgra8888(
    4,
    redShift: 16,
    redBits: 8,
    greenBits: 8,
    greenShift: 8,
    blueShift: 0,
    blueBits: 8,
    alphaShift: 24,
    alphaBits: 8,
  );

  final int bpp;
  final int redShift;
  final int redBits;
  final int greenShift;
  final int greenBits;
  final int blueShift;
  final int blueBits;

  final bool hasAlpha;
  final int alphaShift;
  final int alphaBits;

  final Endian endian;

  const PixelFormat(
    this.bpp, {
    required this.redShift,
    required this.redBits,
    required this.greenShift,
    required this.greenBits,
    required this.blueShift,
    required this.blueBits,
    required this.alphaShift,
    required this.alphaBits,
    this.endian = Endian.little,
  }) : hasAlpha = alphaBits != 0 || alphaBits != 0;

  const PixelFormat.opaque(
    this.bpp, {
    required this.redShift,
    required this.redBits,
    required this.greenShift,
    required this.greenBits,
    required this.blueShift,
    required this.blueBits,
    this.endian = Endian.little,
  })  : hasAlpha = false,
        alphaBits = 0,
        alphaShift = 0;

  int _swapByteOrder(int value) {
    switch (bpp) {
      case 1:
        return value;
      case 2:
        return ((value & 0xFF) << 8) | ((value & 0xFF00) >> 8);
      case 4:
        return ((value & 0xFF) << 24) |
            ((value & 0xFF00) << 8) |
            ((value & 0xFF0000) >> 8) |
            ((value & 0xFF000000) >> 24);
      default:
        throw StateError('Unsupported byte order swap for $bpp bytes.');
    }
  }

  ({int r, int g, int b, int a}) getComponents(int value) {
    if (endian == Endian.big) {
      value = _swapByteOrder(value);
    }

    return (
      r: value >> redShift & ((1 << redBits) - 1),
      g: value >> greenShift & ((1 << greenBits) - 1),
      b: value >> blueShift & ((1 << blueShift) - 1),
      a: value >> alphaShift & ((1 << alphaBits) - 1),
    );
  }

  int fromComponents(int r, int g, int b, int a) {
    var value = 0;

    value |= (r & ((1 << redBits) - 1)) << redShift;
    value |= (g & ((1 << greenBits) - 1)) << greenShift;
    value |= (b & ((1 << blueBits) - 1)) << blueShift;
    value |= (a & ((1 << alphaBits) - 1)) << alphaShift;

    if (endian == Endian.big) {
      value = _swapByteOrder(value);
    }

    return value;
  }

  static int _convertBits(int value, int sourceBits, int destBits) {
    if (sourceBits > destBits) {
      return value >> (sourceBits - destBits);
    } else if (destBits > sourceBits) {
      return value << (destBits - sourceBits);
    } else {
      return value;
    }
  }

  static int convert(
    int source, {
    required PixelFormat sourceFormat,
    required PixelFormat destFormat,
  }) {
    final components = sourceFormat.getComponents(source);

    return destFormat.fromComponents(
      _convertBits(components.r, sourceFormat.redBits, destFormat.redBits),
      _convertBits(components.g, sourceFormat.greenBits, destFormat.greenBits),
      _convertBits(components.b, sourceFormat.blueBits, destFormat.blueBits),
      _convertBits(components.a, sourceFormat.alphaBits, destFormat.alphaBits),
    );
  }
}

class ImageData {
  ImageData(
    this.bytes, {
    required this.format,
    required this.width,
    required this.height,
    int? stride,
  }) : stride = stride ?? width * format.bpp;

  final Uint8List bytes;
  final PixelFormat format;
  int get bpp => format.bpp;
  final int stride;
  final int width;
  final int height;

  static Future<ImageData> fromDartUIImage(ui.Image image) async {
    final byteData = await image.toByteData(
      format: ui.ImageByteFormat.rawStraightRgba,
    );
    if (byteData == null) {
      throw StateError('Image encoding failed.');
    }

    final buffer = byteData.buffer.asUint8List();

    return ImageData(
      buffer,
      format: PixelFormat.rgba8888,
      width: image.width,
      height: image.height,
    );
  }

  ImageData convert(PixelFormat format) {
    if (this.format == format) {
      return this;
    } else {
      final targetBytes =
          PixelFormatConverter(this.format, format).convert(bytes);

      return ImageData(
        targetBytes,
        format: format,
        width: width,
        height: height,
      );
    }
  }

  ImageData toRGB565() {
    return convert(PixelFormat.rgb565);
  }

  int getOffset(int x, int y) {
    return (y * stride) + x * bpp;
  }

  (int, int) getCoordinatesForOffset(int offset) {
    final y = offset ~/ stride;
    final x = (offset - y * stride) ~/ bpp;

    return (x, y);
  }

  Uint8List getPixelBytes(int x, int y) {
    final offset = (y * stride) + x * bpp;
    return bytes.sublistView(offset, offset + bpp);
  }

  int getPixelValue(
    int x,
    int y, {
    Endian endian = Endian.little,
    PixelFormat? format,
  }) {
    final bytes = getPixelBytes(x, y);
    assert(bytes.length <= 8);

    final pixelValue = bytes.reversed.fold(0, (acc, byte) => acc << 8 | byte);
    if (format == null || format == this.format) {
      return pixelValue;
    } else {
      return PixelFormat.convert(
        pixelValue,
        sourceFormat: this.format,
        destFormat: format,
      );
    }
  }

  Iterable<int> getRow(int y) {
    return Iterable.generate(width, (x) {
      return getPixelValue(x, y);
    });
  }

  ImageData view(IntRect rect) {
    // assert rect is within bounds
    assert(rect.left >= 0);
    assert(rect.top >= 0);
    assert(rect.right <= width);
    assert(rect.bottom <= height);

    final startOffset = getOffset(rect.left, rect.top);

    final newStride = getOffset(rect.left, rect.top + 1) - startOffset;

    final newBytes = bytes.sublistView(startOffset);

    return ImageData(
      newBytes,
      format: format,
      width: rect.width,
      height: rect.height,
      stride: newStride,
    );
  }
}

class DiscardingByteDataWriter implements ByteDataWriter {
  DiscardingByteDataWriter({this.endian = Endian.big});

  @override
  int bufferLength = 0;

  int _length = 0;
  int get length => _length;

  @override
  final Endian endian;

  @override
  Uint8List toBytes() {
    return Uint8List(0);
  }

  @override
  void write(List<int> bytes, {bool copy = false}) {
    _length += bytes.length;
  }

  @override
  void writeFloat32(double value, [Endian? endian]) {
    _length += 4;
  }

  @override
  void writeFloat64(double value, [Endian? endian]) {
    _length += 8;
  }

  @override
  void writeInt(int byteLength, int value, [Endian? endian]) {
    _length += byteLength;
  }

  @override
  void writeInt16(int value, [Endian? endian]) {
    _length += 2;
  }

  @override
  void writeInt32(int value, [Endian? endian]) {
    _length += 4;
  }

  @override
  void writeInt64(int value, [Endian? endian]) {
    _length += 8;
  }

  @override
  void writeInt8(int value) {
    _length += 1;
  }

  @override
  void writeUint(int byteLength, int value, [Endian? endian]) {
    _length += byteLength;
  }

  @override
  void writeUint16(int value, [Endian? endian]) {
    _length += 2;
  }

  @override
  void writeUint32(int value, [Endian? endian]) {
    _length += 4;
  }

  @override
  void writeUint64(int value, [Endian? endian]) {
    _length += 8;
  }

  @override
  void writeUint8(int value) {
    _length += 1;
  }
}

class IntRect {
  const IntRect.fromLTRB(this.left, this.top, this.right, this.bottom);

  const IntRect.fromLTWH(int left, int top, int width, int height)
      : this.fromLTRB(left, top, left + width, top + height);

  IntRect.fromCircle({required (int, int) center, required int radius})
      : this.fromCenter(
          center: center,
          width: radius * 2,
          height: radius * 2,
        );

  /// Constructs a rectangle from its center point, width, and height.
  ///
  /// The `center` argument is assumed to be an offset from the origin.
  IntRect.fromCenter({
    required (int, int) center,
    required int width,
    required int height,
  }) : this.fromLTRB(
          center.$1 - width ~/ 2,
          center.$2 - height ~/ 2,
          center.$1 + width ~/ 2,
          center.$2 + height ~/ 2,
        );

  /// Construct the smallest rectangle that encloses the given offsets, treating
  /// them as vectors from the origin.
  IntRect.fromPoints((int, int) a, (int, int) b)
      : this.fromLTRB(
          math.min(a.$1, b.$1),
          math.min(a.$2, b.$2),
          math.max(a.$1, b.$1),
          math.max(a.$2, b.$2),
        );

  /// The offset of the left edge of this rectangle from the x axis.
  final int left;

  /// The offset of the top edge of this rectangle from the y axis.
  final int top;

  /// The offset of the right edge of this rectangle from the x axis.
  final int right;

  /// The offset of the bottom edge of this rectangle from the y axis.
  final int bottom;

  /// The distance between the left and right edges of this rectangle.
  int get width => right - left;

  /// The distance between the top and bottom edges of this rectangle.
  int get height => bottom - top;

  /// A rectangle with left, top, right, and bottom edges all at zero.
  static const IntRect zero = IntRect.fromLTRB(0, 0, 0, 0);

  /// Whether this rectangle encloses a non-zero area. Negative areas are
  /// considered empty.
  bool get isEmpty => left >= right || top >= bottom;

  /// Returns a new rectangle translated by the given offset.
  ///
  /// To translate a rectangle by separate x and y components rather than by an
  /// [Offset], consider [translate].
  IntRect shift((int, int) offset) {
    return IntRect.fromLTRB(
      left + offset.$1,
      top + offset.$2,
      right + offset.$1,
      bottom + offset.$2,
    );
  }

  /// Returns a new rectangle with translateX added to the x components and
  /// translateY added to the y components.
  ///
  /// To translate a rectangle by an [Offset] rather than by separate x and y
  /// components, consider [shift].
  IntRect translate(int translateX, int translateY) {
    return IntRect.fromLTRB(
      left + translateX,
      top + translateY,
      right + translateX,
      bottom + translateY,
    );
  }

  /// Returns a new rectangle with edges moved outwards by the given delta.
  IntRect inflate(int delta) {
    return IntRect.fromLTRB(
      left - delta,
      top - delta,
      right + delta,
      bottom + delta,
    );
  }

  /// Returns a new rectangle with edges moved inwards by the given delta.
  IntRect deflate(int delta) => inflate(-delta);

  /// Returns a new rectangle that is the intersection of the given
  /// rectangle and this rectangle. The two rectangles must overlap
  /// for this to be meaningful. If the two rectangles do not overlap,
  /// then the resulting Rect will have a negative width or height.
  IntRect intersect(IntRect other) {
    return IntRect.fromLTRB(
      math.max(left, other.left),
      math.max(top, other.top),
      math.min(right, other.right),
      math.min(bottom, other.bottom),
    );
  }

  /// Returns a new rectangle which is the bounding box containing all the
  /// points in both this rectangle and the given rectangle.
  IntRect expandToInclude(IntRect other) {
    return switch ((isEmpty, other.isEmpty)) {
      (true, true) => IntRect.zero,
      (true, false) => other,
      (false, true) => this,
      (false, false) => IntRect.fromLTRB(
          math.min(left, other.left),
          math.min(top, other.top),
          math.max(right, other.right),
          math.max(bottom, other.bottom),
        ),
    };
  }

  /// Whether `other` has a nonzero area of overlap with this rectangle.
  bool overlaps(IntRect other) {
    if (right <= other.left || other.right <= left) {
      return false;
    }
    if (bottom <= other.top || other.bottom <= top) {
      return false;
    }
    return true;
  }

  /// The lesser of the magnitudes of the [width] and the [height] of this
  /// rectangle.
  int get shortestSide => math.min(width.abs(), height.abs());

  /// The greater of the magnitudes of the [width] and the [height] of this
  /// rectangle.
  int get longestSide => math.max(width.abs(), height.abs());

  /// The offset to the intersection of the top and left edges of this rectangle.
  (int, int) get topLeft => (left, top);

  /// The offset to the center of the top edge of this rectangle.
  (int, int) get topCenter => (left + width ~/ 2, top);

  /// The offset to the intersection of the top and right edges of this rectangle.
  (int, int) get topRight => (right, top);

  /// The offset to the center of the left edge of this rectangle.
  (int, int) get centerLeft => (left, top + height ~/ 2);

  /// The offset to the point halfway between the left and right and the top and
  /// bottom edges of this rectangle.
  (int, int) get center => (left + width ~/ 2, top + height ~/ 2);

  /// The offset to the center of the right edge of this rectangle.
  (int, int) get centerRight => (right, top + height ~/ 2);

  /// The offset to the intersection of the bottom and left edges of this rectangle.
  (int, int) get bottomLeft => (left, bottom);

  /// The offset to the center of the bottom edge of this rectangle.
  (int, int) get bottomCenter => (left + width ~/ 2, bottom);

  /// The offset to the intersection of the bottom and right edges of this rectangle.
  (int, int) get bottomRight => (right, bottom);

  /// Whether the point specified by the given offset (which is assumed to be
  /// relative to the origin) lies between the left and right and the top and
  /// bottom edges of this rectangle.
  ///
  /// Rectangles include their top and left edges but exclude their bottom and
  /// right edges.
  bool contains((int, int) offset) {
    final (x, y) = offset;
    return x >= left && x < right && y >= top && y < bottom;
  }

  @override
  bool operator ==(Object other) {
    if (identical(this, other)) {
      return true;
    }
    if (runtimeType != other.runtimeType) {
      return false;
    }
    return other is IntRect &&
        other.left == left &&
        other.top == top &&
        other.right == right &&
        other.bottom == bottom;
  }

  @override
  int get hashCode => Object.hash(left, top, right, bottom);

  @override
  String toString() =>
      'IntRect.fromLTRB(${left.toStringAsFixed(1)}, ${top.toStringAsFixed(1)}, ${right.toStringAsFixed(1)}, ${bottom.toStringAsFixed(1)})';
}

abstract class ByteSerializable {
  void write(ByteDataWriter writer);
}
