import 'package:flutter/rendering.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_remote_display/src/display.dart';

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

  void _schedulePostFrameCallback() {
    SchedulerBinding.instance.addPostFrameCallback(_onPostFrameCallback);
  }

  void _onPostFrameCallback(Duration timestamp) async {
    if (!mounted) return;

    if (!widget.connection.isConnected) return;

    final renderObject = _repaintBoundaryKey.currentContext?.findRenderObject()
        as RenderRepaintBoundary?;

    if (renderObject == null) {
      return;
    }

    final frame = await renderObject.toImage();
    if (!mounted) return;

    widget.connection.add(frame);

    frame.dispose();

    _schedulePostFrameCallback();
  }

  @override
  void initState() {
    super.initState();

    _schedulePostFrameCallback();
  }

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      key: _repaintBoundaryKey,
      child: widget.child,
    );
  }
}
