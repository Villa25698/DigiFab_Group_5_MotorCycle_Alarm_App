// =============================================================================
// MC Alarm — Scan & connect screen
// =============================================================================
//
// Scans for the master ("MC-Alarm") and connects to the first match.
// There is no device picker because there's only ever one gateway in the
// rider's setup, so a list would just add a tap.
//
// Flow:
//   1. initState fires _startScan after the first frame.
//   2. _startScan asks BleService to scan; on success, asks it to connect.
//   3. On successful connect, pop everything off the navigator stack so the
//      root router (in main.dart) renders the dashboard underneath.
//   4. On failure, show a "Try again" button.
// =============================================================================

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../bluetooth/ble_service.dart';
import '../theme/app_theme.dart';

class ScanScreen extends StatefulWidget {
  const ScanScreen({super.key});

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  String _status = 'Searching for MC-Alarm…';
  bool _failed = false;

  @override
  void initState() {
    super.initState();
    // Can't context.read() inside initState directly. Defer to next frame.
    WidgetsBinding.instance.addPostFrameCallback((_) => _startScan());
  }

  Future<void> _startScan() async {
    setState(() {
      _status = 'Searching for MC-Alarm…';
      _failed = false;
    });
    final ble = context.read<BleService>();

    // STEP 1 — scan
    final device = await ble.scanForGateway();
    if (!mounted) return;     // user might have backed out during the scan
    if (device == null) {
      setState(() {
        _status = 'Could not find the gateway.';
        _failed = true;
      });
      return;
    }

    // STEP 2 — connect
    setState(() => _status = 'Connecting…');
    final ok = await ble.connect(device);
    if (!mounted) return;
    if (!ok) {
      setState(() {
        _status = 'Connection failed.';
        _failed = true;
      });
      return;
    }

    // STEP 3 — clean up the navigator stack.
    // We pushed PermissionsScreen then ScanScreen on top of WelcomeScreen.
    // Now that we're connected, the root router will render DashboardScreen
    // underneath, but only if we pop those overlays off. popUntil(isFirst)
    // wipes everything back to the root.
    Navigator.of(context).popUntil((route) => route.isFirst);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Connect to gateway')),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Spacer(),
              // Spinner while we're working; error icon if we gave up.
              if (!_failed)
                const CircularProgressIndicator(
                    color: AppTheme.primary, strokeWidth: 6),
              if (_failed)
                const Icon(Icons.error_outline,
                    size: 96, color: AppTheme.danger),
              const SizedBox(height: 28),
              Text(_status,
                  textAlign: TextAlign.center,
                  style: const TextStyle(fontSize: 22)),
              const Spacer(),
              if (_failed)
                ElevatedButton(
                    onPressed: _startScan, child: const Text('Try again')),
              const SizedBox(height: 24),
            ],
          ),
        ),
      ),
    );
  }
}
