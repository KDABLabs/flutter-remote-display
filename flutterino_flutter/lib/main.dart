import 'package:flutter/material.dart';

import 'package:permission_handler/permission_handler.dart';

import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';

import 'package:flutter_remote_display/flutter_remote_display.dart';

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

class _MyHomePageState extends State<MyHomePage> {
  static const address = '8C:AA:B5:83:82:0A';

  static const remoteDisplayDimensions = Size(240, 240);

  late Future<BluetoothDisplayConnection> _connection;

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
  void initState() {
    super.initState();

    _connection = connectToDevice();
  }

  @override
  void dispose() {
    _connection.then((value) {
      value.close();
    });

    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        title: const Text('flutter-remote-display test'),
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: () {
          setState(() {
            _connection.then((value) => value.close());
            _connection = connectToDevice();
          });
        },
        child: const Icon(Icons.refresh),
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            FutureBuilder(
              future: _connection,
              builder: (context, snapshot) {
                if (snapshot.hasError) {
                  return ErrorWidget(snapshot.error!);
                } else if (snapshot.hasData) {
                  final displayConnection = snapshot.requireData;

                  return SizedBox.fromSize(
                    size: remoteDisplayDimensions,
                    child: DecoratedBox(
                      position: DecorationPosition.foreground,
                      decoration: BoxDecoration(
                        border: Border.all(
                          strokeAlign: BorderSide.strokeAlignOutside,
                        ),
                      ),
                      child: TickerMode(
                        // don't animate the flutter logo
                        enabled: true,

                        child: RemoteView(
                          connection: displayConnection,
                          child: InteractiveRemoteTestWidget(),
                        ),
                      ),
                    ),
                  );
                } else {
                  return const CircularProgressIndicator();
                }
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

  final MaterialStateProperty<Icon?> thumbIcon =
      MaterialStateProperty.resolveWith<Icon?>(
    (Set<MaterialState> states) {
      if (states.contains(MaterialState.selected)) {
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

class AnimatingRemoteTestWidget extends StatelessWidget {
  const AnimatingRemoteTestWidget({
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
