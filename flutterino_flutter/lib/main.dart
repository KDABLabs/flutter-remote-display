import 'dart:async';
import 'dart:typed_data';
import 'dart:ui';

import 'package:flutter/material.dart';

import 'package:permission_handler/permission_handler.dart';

import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';

import 'package:flutter_remote_display/flutter_remote_display.dart';
import 'package:wonders/wonders.dart' as wonders;

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Flutter Demo',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.deepPurple),
        useMaterial3: true,
      ),
      home: const MyHomePage(),
    );
  }
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({super.key});

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class WatchDecoration extends StatelessWidget {
  const WatchDecoration({
    super.key,
    required this.child,
  });

  final Widget child;

  static const borderVertical = 120.0;
  static const borderHorizontal = 60.0;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(
        vertical: borderVertical / 2,
        horizontal: borderHorizontal / 2,
      ),
      child: DecoratedBox(
        position: DecorationPosition.background,
        decoration: const BoxDecoration(
          color: Colors.black,
          borderRadius: BorderRadius.all(
            Radius.circular(30),
          ),
          border: Border.symmetric(
            vertical: BorderSide(
              color: Colors.black,
              width: borderVertical,
            ),
            horizontal: BorderSide(
              color: Colors.black,
              width: borderHorizontal,
            ),
          ),
        ),
        child: Padding(
          padding: const EdgeInsets.symmetric(
            vertical: borderVertical / 2,
            horizontal: borderHorizontal / 2,
          ),
          child: child,
        ),
      ),
    );
  }
}

class _MyHomePageState extends State<MyHomePage> {
  static const address = '08:B6:1F:C1:75:BA';

  static const remoteDisplayDimensions = Size(240, 240);

  Future<BluetoothDisplayConnection>? _connection;

  final _viewKey = GlobalKey(debugLabel: 'remote view');

  Future<BluetoothDisplayConnection> connectToDevice() async {
    if (!(await Permission.bluetoothScan.request()).isGranted) {
      throw Exception('No permission to scan for bluetooth devices.');
    }

    if (!(await Permission.bluetoothConnect.request()).isGranted) {
      throw Exception('No permission to connect to bluetooth devices.');
    }

    final bluetooth = FlutterBluetoothSerial.instance;

    var result = await bluetooth.requestEnable();
    if (result != true) {
      throw Exception('Failed to enable bluetooth.');
    }

    final state = await bluetooth.getBondStateForAddress(address);
    if (!state.isBonded) {
      result = await bluetooth.bondDeviceAtAddress(address);
      if (result != true) {
        throw Exception('Could not bond device with address "$address"');
      }
    }

    return await BluetoothDisplayConnection.connect(address);
  }

  @override
  void dispose() {
    _connection?.then((value) {
      value.close();
    });
    _connection = null;

    super.dispose();
  }

  Widget buildView() {
    return WatchfaceCarousel(
      children: [
        const FlutterinoLoadingWatchface(),
        BasicWatchface(key: _viewKey),
        const WondersWatchface(),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        title: const Text('flutter-remote-display test'),
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            FutureBuilder(
              future: _connection,
              builder: (context, snapshot) {
                late final Widget view;
                if (snapshot.data case BluetoothDisplayConnection connection) {
                  view = RemoteView(
                    connection: connection,
                    child: buildView(),
                  );
                } else {
                  view = buildView();
                }

                final isConnected =
                    snapshot.connectionState == ConnectionState.done &&
                        !snapshot.hasError;

                final isError =
                    snapshot.connectionState == ConnectionState.done &&
                        snapshot.hasError;

                final isConnecting =
                    snapshot.connectionState == ConnectionState.waiting;

                return Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    WatchDecoration(
                      child: SizedBox.fromSize(
                        size: remoteDisplayDimensions,
                        child: DecoratedBox(
                          position: DecorationPosition.foreground,
                          decoration: BoxDecoration(
                            border: Border.all(
                              strokeAlign: BorderSide.strokeAlignOutside,
                            ),
                          ),
                          child: view,
                        ),
                      ),
                    ),
                    Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Flexible(
                          flex: 1,
                          child: Row(
                            mainAxisAlignment: MainAxisAlignment.end,
                            children: [
                              if (isConnected)
                                const Text('Connected')
                              else if (isError)
                                Text(
                                  'Connection failed, reason: ${snapshot.error}',
                                )
                              else if (isConnecting)
                                const Text('Connecting...')
                              else
                                const Text('Not connected'),
                            ],
                          ),
                        ),
                        const SizedBox(width: 16),
                        Flexible(
                          flex: 1,
                          child: Align(
                            alignment: Alignment.centerLeft,
                            heightFactor: 1,
                            child: ElevatedButton(
                              onPressed: () {
                                setState(() {
                                  _connection?.then((value) {
                                    value.close();
                                  });
                                  _connection = null;

                                  _connection = connectToDevice();
                                });
                              },
                              child: const Text('Reconnect'),
                            ),
                          ),
                        ),
                      ],
                    )
                  ],
                );
              },
            ),
          ],
        ),
      ),
    );
  }
}

class InteractiveRemoteTestWidget extends StatelessWidget {
  InteractiveRemoteTestWidget({
    super.key,
  });

  final thumbIcon = WidgetStateProperty.resolveWith<Icon?>(
    (Set<WidgetState> states) {
      if (states.contains(WidgetState.selected)) {
        return const Icon(Icons.check);
      }
      return const Icon(Icons.close);
    },
  );

  @override
  Widget build(BuildContext context) {
    var checked = false;

    return MaterialApp(
      home: Center(
        child: Card(
          color: Colors.white,
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Tooltip(
              message: 'Flutterino 0.1',
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const FlutterLogo(size: 18),
                  const Text('lutterino'),
                  const SizedBox(width: 18),
                  StatefulBuilder(
                    builder: (context, setState) {
                      return Switch(
                        value: checked,
                        thumbIcon: thumbIcon,
                        onChanged: (value) {
                          setState(() {
                            checked = value;
                          });
                        },
                      );
                    },
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class FlutterinoLoadingWatchface extends StatelessWidget {
  const FlutterinoLoadingWatchface({
    super.key,
  });

  @override
  Widget build(BuildContext context) {
    return const Center(
      child: Card(
        color: Colors.white,
        child: Padding(
          padding: EdgeInsets.all(12),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              FlutterLogo(
                size: 18,
              ),
              Text('lutterino loading...'),
              SizedBox(
                width: 18,
              ),
              SizedBox.square(
                dimension: 15,
                child: CircularProgressIndicator(),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

final brightnessThumbIcon = WidgetStateProperty.resolveWith<Icon?>(
  (Set<WidgetState> states) {
    if (states.contains(WidgetState.selected)) {
      return const Icon(Icons.light_mode);
    }

    return const Icon(Icons.dark_mode);
  },
);

final checkedUncheckedThumbIcon = WidgetStateProperty.resolveWith<Icon?>(
  (Set<WidgetState> states) {
    if (states.contains(WidgetState.selected)) {
      return const Icon(Icons.check);
    }

    return const Icon(Icons.close);
  },
);

final bluetoothThumbIcon = WidgetStateProperty.resolveWith<Icon?>(
  (Set<WidgetState> states) {
    if (states.contains(WidgetState.selected)) {
      return const Icon(Icons.bluetooth);
    }

    return const Icon(Icons.bluetooth_disabled);
  },
);

final wifiThumbIcon = WidgetStateProperty.resolveWith<Icon?>(
  (Set<WidgetState> states) {
    if (states.contains(WidgetState.selected)) {
      return const Icon(Icons.wifi);
    }

    return const Icon(Icons.wifi_off);
  },
);

class BasicWatchface extends StatefulWidget {
  const BasicWatchface({
    super.key,
  });

  @override
  State<BasicWatchface> createState() => _BasicWatchfaceState();
}

class _BasicWatchfaceState extends State<BasicWatchface> {
  var time = DateTime.now();
  var theme = ThemeMode.light;
  var bluetooth = false;
  var wifi = false;

  late final Timer timer;

  String twentyFourHourTime({DateTime? time, bool seconds = true}) {
    time ??= DateTime.now();

    final hour = time.hour.toString().padLeft(2, '0');
    final minute = time.minute.toString().padLeft(2, '0');
    final second = time.second.toString().padLeft(2, '0');

    return '$hour:$minute${seconds ? ':$second' : ''}';
  }

  @override
  void initState() {
    super.initState();

    timer = Timer.periodic(const Duration(seconds: 1), (timer) {
      setState(() {
        time = DateTime.now();
      });
    });
  }

  @override
  void dispose() {
    timer.cancel();

    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      themeMode: theme,
      theme: ThemeData.light(useMaterial3: true),
      darkTheme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: Colors.black,
      ),
      home: Builder(
        builder: (context) {
          return ColoredBox(
            color: Theme.of(context).scaffoldBackgroundColor,
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                mainAxisSize: MainAxisSize.max,
                children: [
                  Row(
                    mainAxisSize: MainAxisSize.max,
                    mainAxisAlignment: MainAxisAlignment.spaceAround,
                    children: [
                      Switch(
                        value: theme == ThemeMode.light,
                        thumbIcon: brightnessThumbIcon,
                        onChanged: (value) {
                          setState(() {
                            theme = switch (value) {
                              true => ThemeMode.light,
                              false => ThemeMode.dark,
                            };
                          });
                        },
                      ),
                      Switch(
                        value: bluetooth,
                        thumbIcon: bluetoothThumbIcon,
                        onChanged: (value) {
                          setState(() {
                            bluetooth = value;
                          });
                        },
                      ),
                      Switch(
                        value: wifi,
                        thumbIcon: wifiThumbIcon,
                        onChanged: (value) {
                          setState(() {
                            wifi = value;
                          });
                        },
                      ),
                    ],
                  ),
                  const Spacer(),
                  Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Icon(
                        Icons.sunny,
                        color: Colors.yellow,
                      ),
                      const SizedBox(width: 4),
                      Text(
                        '27°C in Berlin',
                        style: Theme.of(context).textTheme.bodyMedium,
                      ),
                    ],
                  ),
                  Align(
                    alignment: Alignment.center,
                    heightFactor: 1,
                    child: Text(
                      twentyFourHourTime(time: time, seconds: false),
                      style: Theme.of(context).textTheme.displayLarge?.copyWith(
                        fontFeatures: [
                          const FontFeature.tabularFigures(),
                        ],
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }
}

class WondersWatchface extends StatefulWidget {
  const WondersWatchface({super.key});

  @override
  State<WondersWatchface> createState() => _WondersWatchfaceState();
}

class PackageAssetBundle implements AssetBundle {
  PackageAssetBundle({
    required this.inner,
    required this.package,
  });

  final AssetBundle inner;
  final String package;

  @override
  void clear() {
    inner.clear();
  }

  String _transformKey(String key) {
    if (key == 'AssetManifest.bin') {
      return key;
    } else {
      return 'packages/$package/$key';
    }
  }

  @override
  void evict(String key) {
    return inner.evict(_transformKey(key));
  }

  @override
  Future<ByteData> load(String key) async {
    return await inner.load(_transformKey(key));
  }

  @override
  Future<ImmutableBuffer> loadBuffer(String key) async {
    return await inner.loadBuffer(_transformKey(key));
  }

  @override
  Future<String> loadString(String key, {bool cache = true}) async {
    return await inner.loadString(_transformKey(key), cache: cache);
  }

  @override
  Future<T> loadStructuredBinaryData<T>(
      String key, FutureOr<T> Function(ByteData data) parser) async {
    return await inner.loadStructuredBinaryData(_transformKey(key), parser);
  }

  @override
  Future<T> loadStructuredData<T>(
      String key, Future<T> Function(String value) parser) async {
    return await inner.loadStructuredData(key, parser);
  }
}

class _WondersWatchfaceState extends State<WondersWatchface> {
  static var _initialized = false;

  @override
  void initState() {
    super.initState();

    if (!_initialized) {
      _initialized = true;
      wonders.registerSingletons();
      wonders.appLogic.bootstrap();
    }
  }

  @override
  Widget build(BuildContext context) {
    return FractionallySizedBox(
      widthFactor: 2,
      heightFactor: 2,
      child: Transform.scale(
        scale: 0.5,
        child: wonders.WondersApp(),
      ),
    );
  }
}

class WatchfaceCarousel extends StatefulWidget {
  const WatchfaceCarousel({
    super.key,
    required this.children,
    this.initialIndex = 0,
  }) : assert(0 <= initialIndex && initialIndex < children.length);

  final List<Widget> children;
  final int initialIndex;

  @override
  State<WatchfaceCarousel> createState() => _WatchfaceCarouselState();
}

class _WatchfaceCarouselState extends State<WatchfaceCarousel> {
  late var index = widget.initialIndex;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onDoubleTap: () {
        final index = (this.index + 1) % widget.children.length;
        setState(() {
          this.index = index;
        });
      },
      behavior: HitTestBehavior.translucent,
      child: widget.children[index],
    );
  }
}
