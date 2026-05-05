// =============================================================================
// MC Alarm — Permissions screen
// =============================================================================
//
// Asks the OS for the permissions BLE needs:
//   - Bluetooth Scan + Connect (required)
//   - Location When In Use     (Android 11 and below need this for BLE scans)
//   - Notifications            (nice-to-have, used for the wobble alert)
//
// We do NOT block on GPS being switched on. Our AndroidManifest sets
// android:usesPermissionFlags="neverForLocation" on BLUETOOTH_SCAN, which
// tells Android 12+ that we are not using BLE to derive location, so it does
// not require the user to turn on the Location services switch.
//
// Once Bluetooth + Location are granted (Notifications is optional), we
// pushReplacement to ScanScreen so the back button doesn't bring the user
// back here.
// =============================================================================

import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';

import '../theme/app_theme.dart';
import 'scan_screen.dart';

class PermissionsScreen extends StatefulWidget {
  const PermissionsScreen({super.key});

  @override
  State<PermissionsScreen> createState() => _PermissionsScreenState();
}

class _PermissionsScreenState extends State<PermissionsScreen> {
  // Per-permission "is granted?" flags drive the green check / grey circle
  // indicators in the UI.
  bool _btGranted    = false;
  bool _locGranted   = false;
  bool _notifGranted = false;

  /// True while a permission request is in flight — we disable the button
  /// so the user can't double-tap.
  bool _working = false;

  /// Status line shown above the button.
  String _status = 'Tap the button to grant permissions.';

  /// Request all the permissions the BLE stack needs, then either advance
  /// to ScanScreen or show an error / open Settings.
  Future<void> _runChecks() async {
    setState(() {
      _working = true;
      _status = 'Requesting permissions…';
    });
    try {
      // Different permissions list per platform. iOS doesn't have the
      // split bluetoothScan / bluetoothConnect — it has a single
      // Bluetooth permission.
      final perms = Platform.isAndroid
          ? <Permission>[
              Permission.bluetoothScan,
              Permission.bluetoothConnect,
              Permission.locationWhenInUse,
              Permission.notification,
            ]
          : <Permission>[
              Permission.bluetooth,
              Permission.locationWhenInUse,
              Permission.notification,
            ];

      // permission_handler request() shows the system dialog for any perm
      // that's currently undetermined. Already-granted perms are skipped.
      final results = await perms.request();
      results.forEach((p, s) => debugPrint('perm $p -> $s'));

      // "limited" is iOS-only and counts as granted for our purposes.
      bool isOk(Permission p) {
        final s = results[p];
        return s == PermissionStatus.granted ||
               s == PermissionStatus.limited;
      }

      _btGranted = Platform.isAndroid
          ? (isOk(Permission.bluetoothScan) &&
             isOk(Permission.bluetoothConnect))
          : isOk(Permission.bluetooth);
      _locGranted   = isOk(Permission.locationWhenInUse);
      _notifGranted = isOk(Permission.notification);

      // "Permanently denied" means the user tapped "Don't ask again". The
      // only way to recover is the system Settings app.
      final permanentlyDenied = results.values
          .any((s) => s == PermissionStatus.permanentlyDenied);

      if (!_btGranted || !_locGranted) {
        if (permanentlyDenied) {
          setState(() => _status =
              'Bluetooth or Location permission was blocked earlier. '
              'Tap the button to open Settings, grant them, then come back.');
          await openAppSettings();
        } else {
          setState(() => _status =
              'Bluetooth and Location permissions are required to scan.');
        }
        return;
      }

      // BT + Location are both OK. We tolerate notifications being denied
      // (the rider just won't get the background-app wobble alert).
      if (mounted) {
        Navigator.of(context).pushReplacement(
            MaterialPageRoute(builder: (_) => const ScanScreen()));
      }
    } catch (e, st) {
      debugPrint('permissions error: $e\n$st');
      setState(() => _status = 'Permissions check failed: $e');
    } finally {
      // mounted check guards against the user backing out during the
      // request — setState on an unmounted widget throws.
      if (mounted) setState(() => _working = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Enable Bluetooth & GPS')),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: 8),
              const Text(
                'The MC Alarm needs Bluetooth and Location permissions so it '
                'can find and talk to the gateway. Notifications are used for '
                'the wobble emergency alert.',
                style: TextStyle(fontSize: 18, color: AppTheme.textSub),
              ),
              const SizedBox(height: 24),
              _Row(label: 'Bluetooth',     ok: _btGranted),
              const SizedBox(height: 10),
              _Row(label: 'Location',      ok: _locGranted),
              const SizedBox(height: 10),
              _Row(label: 'Notifications', ok: _notifGranted),
              const Spacer(),
              Text(_status,
                  textAlign: TextAlign.center,
                  style: const TextStyle(
                      fontSize: 16, color: AppTheme.textSub)),
              const SizedBox(height: 16),
              ElevatedButton(
                onPressed: _working ? null : _runChecks,
                child: Text(_working ? 'Please wait…' : 'Grant & continue'),
              ),
              const SizedBox(height: 24),
            ],
          ),
        ),
      ),
    );
  }
}

/// One row in the permissions checklist. Green check or grey circle.
class _Row extends StatelessWidget {
  final String label;
  final bool ok;
  const _Row({required this.label, required this.ok});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 18),
      decoration: BoxDecoration(
        color: AppTheme.surface,
        borderRadius: BorderRadius.circular(14),
      ),
      child: Row(children: [
        Icon(ok ? Icons.check_circle : Icons.radio_button_unchecked,
            color: ok ? AppTheme.success : AppTheme.textSub, size: 28),
        const SizedBox(width: 16),
        Text(label, style: const TextStyle(fontSize: 20)),
      ]),
    );
  }
}
