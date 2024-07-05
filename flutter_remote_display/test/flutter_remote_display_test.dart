import 'package:flutter_remote_display/flutter_remote_display.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  const blueRgba8888 = 0xFFFF0000;

  test('ARGB to RGB565 conversion', () {
    final rgb565 = PixelFormat.convert(
      blueRgba8888,
      sourceFormat: PixelFormat.rgba8888,
      destFormat: PixelFormat.rgb565,
    );

    expect(rgb565, 0xF800);
  });

  test('ARGB to RGB565 big endian conversion', () {
    final rgb565 = PixelFormat.convert(
      blueRgba8888,
      sourceFormat: PixelFormat.rgba8888,
      destFormat: PixelFormat.rgb565BigEndian,
    );

    expect(rgb565, 0x00F8);
  });
}
