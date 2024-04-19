import 'dart:ui' as ui;

import 'package:flutter_remote_display/src/protocol.dart';

class RemoteDisplayException implements Exception {
  RemoteDisplayException(this.message);

  RemoteDisplayException.connectionLost() : message = 'Connection lost';

  final String message;

  @override
  String toString() {
    return 'Remote Display Exception: $message';
  }
}

abstract class DisplayConnection {
  Sink<HostToDisplayPacket> get output;
  Stream<DisplayToHostPacket> get input;

  Stream<TouchEvent> get touchEvents {
    return input.where((event) => event is TouchEvent).cast<TouchEvent>();
  }

  Future<void> addFrame(ui.Image image);

  void setBacklight(double intensity) {
    return output.add(BacklightPacket(intensity));
  }

  void vibrate(Duration duration) {
    return output.add(VibrationPacket(duration));
  }

  Future<Duration> ping() async {
    final watch = Stopwatch()..start();

    output.add(PingPacket());
    await input.firstWhere((event) => event is PongPacket);

    watch.stop();

    return watch.elapsed;
  }

  Future<void> close();
}
