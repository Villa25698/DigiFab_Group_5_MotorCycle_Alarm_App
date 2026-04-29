import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import 'ble_constants.dart';

enum BleConnectionState { disconnected, scanning, connecting, connected }

/// Owns the BLE connection to the MC-Alarm gateway.
/// All UI reads from this via [ChangeNotifier] + the [wobbleStream].
class BleService extends ChangeNotifier {
  BleConnectionState _state = BleConnectionState.disconnected;
  BleConnectionState get state => _state;

  BluetoothDevice? _device;
  BluetoothDevice? get device => _device;

  bool _alarmActive = false;
  bool get alarmActive => _alarmActive;

  int _nodeStatusMask = 0;
  int get nodeStatusMask => _nodeStatusMask;

  BluetoothCharacteristic? _chrAlarm;
  BluetoothCharacteristic? _chrWobble;
  BluetoothCharacteristic? _chrCmd;
  BluetoothCharacteristic? _chrNodes;

  final _subs = <StreamSubscription<dynamic>>[];

  /// Broadcasts every wobble-event notification. The UI subscribes to pop the
  /// emergency screen. Kept as a stream (not a Notifier value) so repeated
  /// wobbles produce distinct events even if the counter-byte wraps.
  final StreamController<int> _wobbleStream = StreamController.broadcast();
  Stream<int> get wobbleStream => _wobbleStream.stream;

  void _setState(BleConnectionState s) {
    _state = s;
    notifyListeners();
  }

  /// Scan for any device advertising the MC-Alarm service.
  /// Returns the first match, or null on timeout.
  Future<BluetoothDevice?> scanForGateway({Duration timeout = const Duration(seconds: 10)}) async {
    _setState(BleConnectionState.scanning);
    try {
      await FlutterBluePlus.startScan(
        withServices: [Guid(BleUuids.service)],
        timeout: timeout,
      );
      final completer = Completer<BluetoothDevice?>();
      late StreamSubscription sub;
      sub = FlutterBluePlus.scanResults.listen((results) {
        for (final r in results) {
          final name = r.device.platformName.isNotEmpty
              ? r.device.platformName
              : r.advertisementData.advName;
          if (name == kDeviceName || _advertisesService(r)) {
            if (!completer.isCompleted) completer.complete(r.device);
            sub.cancel();
            FlutterBluePlus.stopScan();
            return;
          }
        }
      });
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

  bool _advertisesService(ScanResult r) {
    return r.advertisementData.serviceUuids
        .any((g) => g.str.toLowerCase() == BleUuids.service.toLowerCase());
  }

  Future<bool> connect(BluetoothDevice device) async {
    _setState(BleConnectionState.connecting);
    _device = device;
    try {
      await device.connect(timeout: const Duration(seconds: 15), autoConnect: false);
      _subs.add(device.connectionState.listen((cs) {
        if (cs == BluetoothConnectionState.disconnected) {
          _setState(BleConnectionState.disconnected);
          _cleanup();
        }
      }));

      final services = await device.discoverServices();
      final svc = services.firstWhere(
        (s) => s.uuid.str.toLowerCase() == BleUuids.service.toLowerCase(),
        orElse: () => throw StateError('MC-Alarm service not found'),
      );

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

      if (_chrAlarm == null || _chrWobble == null || _chrCmd == null || _chrNodes == null) {
        throw StateError('Missing expected characteristic on gateway');
      }

      // Prime current values.
      final alarmBytes = await _chrAlarm!.read();
      if (alarmBytes.isNotEmpty) _alarmActive = alarmBytes[0] != 0;
      final nodeBytes = await _chrNodes!.read();
      if (nodeBytes.isNotEmpty) _nodeStatusMask = nodeBytes[0];

      // Subscribe to notifications.
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

      await _chrWobble!.setNotifyValue(true);
      // The first emission after subscribing is the BLE stack's cached value,
      // not a fresh wobble event — skip it. After that, only fire when the
      // counter actually changes (filters out duplicate notifies and the
      // master's boot-time seed value).
      bool firstWobbleEmission = true;
      int? lastWobbleSeen;
      _subs.add(_chrWobble!.lastValueStream.listen((b) {
        if (b.isEmpty) return;
        final v = b[0];
        if (firstWobbleEmission) {
          firstWobbleEmission = false;
          lastWobbleSeen = v;
          return;
        }
        if (v == lastWobbleSeen) return;
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

  Future<void> disconnect() async {
    try { await _device?.disconnect(); } catch (_) {}
    _cleanup();
    _setState(BleConnectionState.disconnected);
  }

  void _cleanup() {
    for (final s in _subs) { s.cancel(); }
    _subs.clear();
    _chrAlarm = _chrWobble = _chrCmd = _chrNodes = null;
    _device = null;
  }

  Future<void> sendCommand(int cmd) async {
    final c = _chrCmd;
    if (c == null) return;
    await c.write([cmd], withoutResponse: false);
  }

  Future<void> stopAlarm()     => sendCommand(BleCmd.stop);
  Future<void> triggerManual() => sendCommand(BleCmd.manualOn);
  Future<void> toggleHazard()  => sendCommand(BleCmd.toggleHazard);

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
