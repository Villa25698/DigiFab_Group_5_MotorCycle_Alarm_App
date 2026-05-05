// =============================================================================
// MC Alarm — BLE service. Owns the connection to the master and exposes
// a stream + ChangeNotifier API for the rest of the app.
// =============================================================================
//
// Lifecycle (happy path):
//
//    disconnected
//        │  scanForGateway()
//        ▼
//      scanning  ───────► (no device found)  ──► disconnected
//        │  found device
//        │  connect(device)
//        ▼
//      connecting
//        │  GATT discovery + subscribe
//        ▼
//      connected
//        │  user disconnects, BLE drops, …
//        ▼
//    disconnected
//
// What this class exposes:
//   - state              : current connection state (drives UI routing)
//   - alarmActive        : last known alarm state from the master
//   - nodeStatusMask     : bitmask, bit n = slave (n+1) online
//   - wobbleStream       : fires every time the master reports a NEW wobble
//
// The UI calls these methods to send commands:
//   - stopAlarm()        : send BleCmd.stop
//   - triggerManual()    : send BleCmd.manualOn
//   - toggleHazard()     : send BleCmd.toggleHazard
// =============================================================================

import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import 'ble_constants.dart';

/// Connection state of the BLE service. Renamed from "ConnectionState" to
/// "BleConnectionState" to avoid collision with Flutter's built-in enum of
/// the same name (used by FutureBuilder etc.).
enum BleConnectionState { disconnected, scanning, connecting, connected }

/// All BLE state lives here. The widget tree reads from it via Provider:
///
///     final ble = context.watch<BleService>();   // rebuild on changes
///     final ble = context.read<BleService>();    // one-shot, no listen
class BleService extends ChangeNotifier {
  // ---------------------------------------------------------------------------
  // STATE
  // ---------------------------------------------------------------------------

  BleConnectionState _state = BleConnectionState.disconnected;
  BleConnectionState get state => _state;

  /// The currently-connected device. null when not connected.
  BluetoothDevice? _device;
  BluetoothDevice? get device => _device;

  /// Last-known alarm state from the master. Updated on connect (initial
  /// read) and on every NOTIFY.
  bool _alarmActive = false;
  bool get alarmActive => _alarmActive;

  /// Bitmask of slaves online. bit 0 = slave 1, bit 1 = slave 2, …
  int _nodeStatusMask = 0;
  int get nodeStatusMask => _nodeStatusMask;

  // ---------------------------------------------------------------------------
  // GATT HANDLES
  // ---------------------------------------------------------------------------
  // Populated during connect(); cleared in _cleanup().

  BluetoothCharacteristic? _chrAlarm;
  BluetoothCharacteristic? _chrWobble;
  BluetoothCharacteristic? _chrCmd;
  BluetoothCharacteristic? _chrNodes;

  /// All stream subscriptions we own (notify listeners + connection state).
  /// Cancelled on disconnect so we don't keep firing into stale state.
  final _subs = <StreamSubscription<dynamic>>[];

  /// Broadcasts every NEW wobble event (counter increment from the master).
  /// The UI subscribes to pop the emergency screen. Kept as a stream (not a
  /// ChangeNotifier value) because the value itself is meaningless — only
  /// the EVENT matters, and we want each event to fire its own listener
  /// callback even if the counter byte happens to equal the previous value
  /// after wraparound at 256.
  final StreamController<int> _wobbleStream = StreamController.broadcast();
  Stream<int> get wobbleStream => _wobbleStream.stream;

  /// Update the connection state and notify any listening widgets.
  void _setState(BleConnectionState s) {
    _state = s;
    notifyListeners();
  }

  // ---------------------------------------------------------------------------
  // SCANNING
  // ---------------------------------------------------------------------------

  /// Scan for a device advertising the MC-Alarm service. Returns the first
  /// match, or null on timeout. There is no device picker — there's only
  /// ever one gateway, so we auto-pick the first hit.
  Future<BluetoothDevice?> scanForGateway(
      {Duration timeout = const Duration(seconds: 10)}) async {
    _setState(BleConnectionState.scanning);
    try {
      // withServices filters at the Android scan level so we don't get
      // every BLE device in the building.
      await FlutterBluePlus.startScan(
        withServices: [Guid(BleUuids.service)],
        timeout: timeout,
      );

      // We need the first matching scan result, then cancel the stream.
      // Completer is the standard Dart pattern for "convert callback to Future".
      final completer = Completer<BluetoothDevice?>();
      late StreamSubscription sub;
      sub = FlutterBluePlus.scanResults.listen((results) {
        for (final r in results) {
          // Pick the device name. Prefer platformName (what the OS sees);
          // fall back to advName (what the device sent in its ad).
          final name = r.device.platformName.isNotEmpty
              ? r.device.platformName
              : r.advertisementData.advName;

          // Match either by name OR service UUID — belt & suspenders since
          // some Android versions strip the name from the scan result.
          if (name == kDeviceName || _advertisesService(r)) {
            if (!completer.isCompleted) completer.complete(r.device);
            sub.cancel();
            FlutterBluePlus.stopScan();
            return;
          }
        }
      });

      // Wait for either a match or the timeout.
      final result = await completer.future.timeout(timeout, onTimeout: () {
        sub.cancel();
        FlutterBluePlus.stopScan();
        return null;
      });

      if (result == null) _setState(BleConnectionState.disconnected);
      return result;
    } catch (e) {
      debugPrint('scan error: $e');
      _setState(BleConnectionState.disconnected);
      return null;
    }
  }

  /// True if this scan result advertised our service UUID.
  bool _advertisesService(ScanResult r) {
    return r.advertisementData.serviceUuids
        .any((g) => g.str.toLowerCase() == BleUuids.service.toLowerCase());
  }

  // ---------------------------------------------------------------------------
  // CONNECT
  // ---------------------------------------------------------------------------

  /// Connect to a device, discover services, prime the cached state values,
  /// and subscribe to all NOTIFY characteristics.
  Future<bool> connect(BluetoothDevice device) async {
    _setState(BleConnectionState.connecting);
    _device = device;
    try {
      await device.connect(
          timeout: const Duration(seconds: 15), autoConnect: false);

      // If the master goes away (battery, out of range, …) reset the
      // service state so the UI swaps back to WelcomeScreen.
      _subs.add(device.connectionState.listen((cs) {
        if (cs == BluetoothConnectionState.disconnected) {
          _setState(BleConnectionState.disconnected);
          _cleanup();
        }
      }));

      // Discover services. Our service should be among them.
      final services = await device.discoverServices();
      final svc = services.firstWhere(
        (s) => s.uuid.str.toLowerCase() == BleUuids.service.toLowerCase(),
        orElse: () => throw StateError('MC-Alarm service not found'),
      );

      // Bind each characteristic we care about by UUID. (We don't rely on
      // discovery order.)
      for (final c in svc.characteristics) {
        final u = c.uuid.str.toLowerCase();
        if (u == BleUuids.alarmState.toLowerCase()) {
          _chrAlarm = c;
        } else if (u == BleUuids.wobbleEvent.toLowerCase()) {
          _chrWobble = c;
        } else if (u == BleUuids.command.toLowerCase()) {
          _chrCmd = c;
        } else if (u == BleUuids.nodeStatus.toLowerCase()) {
          _chrNodes = c;
        }
      }

      if (_chrAlarm == null ||
          _chrWobble == null ||
          _chrCmd == null ||
          _chrNodes == null) {
        throw StateError('Missing expected characteristic on gateway');
      }

      // ---- Prime current values via READ ----
      // Without this, the UI would show "alarm off" until the first NOTIFY
      // arrives (could be several seconds).
      final alarmBytes = await _chrAlarm!.read();
      if (alarmBytes.isNotEmpty) _alarmActive = alarmBytes[0] != 0;
      final nodeBytes = await _chrNodes!.read();
      if (nodeBytes.isNotEmpty) _nodeStatusMask = nodeBytes[0];

      // ---- Subscribe to NOTIFYs ----
      // setNotifyValue(true) writes to the CCC descriptor on the master to
      // tell it "I want to receive notifications". After that, every
      // notify shows up in lastValueStream.

      await _chrAlarm!.setNotifyValue(true);
      _subs.add(_chrAlarm!.lastValueStream.listen((b) {
        if (b.isEmpty) return;
        _alarmActive = b[0] != 0;
        notifyListeners();
      }));

      await _chrNodes!.setNotifyValue(true);
      _subs.add(_chrNodes!.lastValueStream.listen((b) {
        if (b.isEmpty) return;
        _nodeStatusMask = b[0];
        notifyListeners();
      }));

      // Read the wobble counter's current value first to use as our
      // baseline. Anything different from this later is a real new wobble.
      // (We used to "skip the first stream emission" instead, but that
      // accidentally swallowed the very first wobble of a session when
      // the BLE stack didn't pre-emit a cached value.)
      final wobbleBytes = await _chrWobble!.read();
      int lastWobbleSeen = wobbleBytes.isNotEmpty ? wobbleBytes[0] : 0;

      await _chrWobble!.setNotifyValue(true);
      _subs.add(_chrWobble!.lastValueStream.listen((b) {
        if (b.isEmpty) return;
        final v = b[0];
        if (v == lastWobbleSeen) return;   // no change — cached re-emit or
                                           // duplicate notify, ignore
        lastWobbleSeen = v;
        _wobbleStream.add(v);
      }));

      _setState(BleConnectionState.connected);
      return true;
    } catch (e) {
      debugPrint('connect error: $e');
      await disconnect();
      return false;
    }
  }

  // ---------------------------------------------------------------------------
  // DISCONNECT
  // ---------------------------------------------------------------------------

  Future<void> disconnect() async {
    try {
      await _device?.disconnect();
    } catch (_) {
      // disconnect can throw if the device was already gone — ignore.
    }
    _cleanup();
    _setState(BleConnectionState.disconnected);
  }

  /// Cancel all subscriptions and forget the GATT handles. Called on
  /// disconnect AND when a connect attempt fails partway through.
  void _cleanup() {
    for (final s in _subs) {
      s.cancel();
    }
    _subs.clear();
    _chrAlarm = _chrWobble = _chrCmd = _chrNodes = null;
    _device = null;
  }

  // ---------------------------------------------------------------------------
  // COMMANDS — phone -> master
  // ---------------------------------------------------------------------------

  /// Write a single-byte command to the master. No-op if not connected.
  Future<void> sendCommand(int cmd) async {
    final c = _chrCmd;
    if (c == null) return;
    await c.write([cmd], withoutResponse: false);
  }

  Future<void> stopAlarm()     => sendCommand(BleCmd.stop);
  Future<void> triggerManual() => sendCommand(BleCmd.manualOn);
  Future<void> toggleHazard()  => sendCommand(BleCmd.toggleHazard);

  // ---------------------------------------------------------------------------
  // CONVENIENCE
  // ---------------------------------------------------------------------------

  /// Decode a single bit out of [_nodeStatusMask]. Slave IDs are 1-based
  /// to match what the user sees.
  bool isNodeOnline(int nodeIdOneBased) {
    return (_nodeStatusMask & (1 << (nodeIdOneBased - 1))) != 0;
  }

  @override
  void dispose() {
    _cleanup();
    _wobbleStream.close();
    super.dispose();
  }
}
