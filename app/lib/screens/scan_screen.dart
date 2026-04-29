import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../bluetooth/ble_service.dart';
import '../theme/app_theme.dart';

/// Scans for the MC-Alarm gateway and connects automatically.
/// No device picker — there's only one gateway, so we match the service UUID
/// and snap to the first hit.
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
    WidgetsBinding.instance.addPostFrameCallback((_) => _startScan());
  }

  Future<void> _startScan() async {
    setState(() { _status = 'Searching for MC-Alarm…'; _failed = false; });
    final ble = context.read<BleService>();
    final device = await ble.scanForGateway();
    if (!mounted) return;
    if (device == null) {
      setState(() { _status = 'Could not find the gateway.'; _failed = true; });
      return;
    }
    setState(() => _status = 'Connecting…');
    final ok = await ble.connect(device);
    if (!mounted) return;
    if (!ok) {
      setState(() { _status = 'Connection failed.'; _failed = true; });
      return;
    }
    // Connected — clear the welcome/permissions/scan stack so the root
    // router can render the dashboard underneath.
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
              if (!_failed)
                const CircularProgressIndicator(
                    color: AppTheme.primary, strokeWidth: 6),
              if (_failed)
                const Icon(Icons.error_outline, size: 96, color: AppTheme.danger),
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
