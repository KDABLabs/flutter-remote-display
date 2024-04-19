import 'dart:async';

import 'package:flutter/gestures.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_remote_display/flutter_remote_display.dart';
import 'package:flutter_remote_display/src/bluetooth_display.dart';
import 'package:flutter_remote_display/src/display.dart';
import 'package:flutter_remote_display/src/protocol.dart';

class RemoteView extends StatefulWidget {
  const RemoteView({
    super.key,
    required this.connection,
    required this.child,
  });

  final BluetoothDisplayConnection connection;
  final Widget child;

  @override
  State<RemoteView> createState() => _RemoteViewState();
}

class _RemoteViewState extends State<RemoteView> {
  final _repaintBoundaryKey = GlobalKey<_RemoteViewState>();
  BoxHitTestResult? _hitTestResult;

  late StreamSubscription _displayToHostSub;

  void _schedulePostFrameCallback() {
    SchedulerBinding.instance.addPostFrameCallback(_onPostFrameCallback);
  }

  Future<void> _captureFrame() async {
    if (!mounted) return;

    if (!widget.connection.isConnected) return;

    final renderObject = _repaintBoundaryKey.currentContext?.findRenderObject()
        as RenderRepaintBoundary?;

    if (renderObject == null) {
      return;
    }

    // ignore: invalid_use_of_protected_member
    final frame = await (renderObject.layer as OffsetLayer).toImage(
      Offset.zero & renderObject.size,
      pixelRatio: 1.0,
    );
    // final frame = await renderObject.toImage();
    if (!mounted) return;

    widget.connection.addFrame(frame);

    frame.dispose();
  }

  void _onPostFrameCallback(Duration timestamp) async {
    await _captureFrame();

    _schedulePostFrameCallback();
  }

  void _handleTouchEvent(TouchEvent event) {
    final (x, y) = event.position;
    final position = Offset(x.toDouble(), y.toDouble());

    final pointerEvent = switch (event.phase) {
      TouchEventPhase.down => PointerDownEvent(
          viewId: 1000,
          device: 1000,
          pointer: 1000,
          position: Offset(x.toDouble(), y.toDouble()),
        ),
      TouchEventPhase.up => const PointerUpEvent(
          viewId: 1000,
          device: 1000,
          pointer: 1000,
        ),
      TouchEventPhase.move => PointerMoveEvent(
          viewId: 1000,
          device: 1000,
          pointer: 1000,
          position: Offset(x.toDouble(), y.toDouble()),
        )
    };

    // We just emulate what the GestureBinding does here.
    if (event.phase == TouchEventPhase.down) {
      _hitTestResult = BoxHitTestResult();

      final renderObject = _repaintBoundaryKey.currentContext
          ?.findRenderObject() as RenderRepaintBoundary?;
      if (renderObject == null) {
        return;
      }

      renderObject.hitTest(
        _hitTestResult!,
        position: position,
      );
      GestureBinding.instance.hitTestInView(_hitTestResult!, position, 1000);
    }

    debugPrint(
      'got touch event $x, $y, phase: ${event.phase}. hit test result: $_hitTestResult',
    );

    for (final entry in _hitTestResult!.path) {
      entry.target.handleEvent(
        pointerEvent.transformed(entry.transform),
        entry,
      );
    }

    if (event.phase == TouchEventPhase.up) {
      _hitTestResult = null;
    }
  }

  void _onDisplayToHostPacket(DisplayToHostPacket packet) {
    switch (packet) {
      case TouchEvent touch:
        _handleTouchEvent(touch);
        break;
    }
  }

  @override
  void initState() {
    super.initState();

    _schedulePostFrameCallback();

    _displayToHostSub = widget.connection.input.listen(_onDisplayToHostPacket);
  }

  @override
  void didUpdateWidget(covariant RemoteView oldWidget) {
    super.didUpdateWidget(oldWidget);

    if (oldWidget.connection.input != widget.connection.input) {
      _displayToHostSub.cancel();
      _displayToHostSub =
          widget.connection.input.listen(_onDisplayToHostPacket);
    }
  }

  @override
  void reassemble() {
    super.reassemble();

    _captureFrame();
  }

  @override
  void dispose() {
    _displayToHostSub.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      key: _repaintBoundaryKey,
      child: widget.child,
    );
  }
}
