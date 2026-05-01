// =============================================================================
// MC Alarm — Dashboard (the home screen while connected)
// =============================================================================
//
// What the rider sees while everything is fine. Deliberately spartan:
//   - one big status card (link / all online / alarm active)
//   - one chip showing whether the hazard relay slave is online
//   - HAZARDS button (with confirmation dialog when activating)
//   - STOP ALARM button (only enabled when alarm is on)
//   - logout (disconnect) icon in the app bar
//
// Big touch targets, high contrast, no fiddly menus — the rider is
// glancing at a handlebar-mounted phone, possibly with gloves on.
// =============================================================================

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../bluetooth/ble_service.dart';
import '../theme/app_theme.dart';

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  /// Handle a press on the HAZARDS button.
  ///
  /// If the alarm is currently OFF, show a confirmation dialog asking
  /// "Test alarm? YES / NO" before firing. This stops accidental presses
  /// from triggering the siren in public.
  ///
  /// If the alarm is currently ON, the button doubles as a "turn off"
  /// shortcut — no confirmation needed.
  Future<void> _onHazardPressed(BuildContext context) async {
    final ble = context.read<BleService>();

    if (ble.alarmActive) {
      // Already on — just toggle off, no dialog.
      await ble.toggleHazard();
      return;
    }

    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Test alarm?'),
        content: const Text(
          'This will sound the siren and flash the hazard lights.\n\n'
          'Is this a test?',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(false),
            child: const Text('NO'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.of(ctx).pop(true),
            child: const Text('YES'),
          ),
        ],
      ),
    );

    if (confirmed == true) {
      await ble.toggleHazard();
    }
  }

  @override
  Widget build(BuildContext context) {
    // watch() rebuilds this screen whenever BleService.notifyListeners()
    // fires — i.e. when alarm state, node status, or connection state changes.
    final ble = context.watch<BleService>();
    final online1 = ble.isNodeOnline(1);

    return Scaffold(
      appBar: AppBar(
        title: const Text('MC Alarm'),
        actions: [
          IconButton(
            onPressed: ble.disconnect,
            icon: const Icon(Icons.logout),
            tooltip: 'Disconnect',
          ),
        ],
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _StatusCard(
                connected:     true,
                alarmActive:   ble.alarmActive,
                anyNodeOnline: online1,
              ),
              const SizedBox(height: 16),
              _NodeChip(id: 1, online: online1),
              const Spacer(),

              // HAZARDS button — confirmation required to turn ON.
              ElevatedButton.icon(
                onPressed: () => _onHazardPressed(context),
                icon: const Icon(Icons.warning_amber_rounded, size: 32),
                label: Text(ble.alarmActive ? 'HAZARDS ON' : 'HAZARDS'),
              ),
              const SizedBox(height: 16),

              // STOP button — disabled (greyed) when there's nothing to stop.
              ElevatedButton.icon(
                onPressed: ble.alarmActive ? ble.stopAlarm : null,
                style: ElevatedButton.styleFrom(
                  backgroundColor: AppTheme.danger,
                  foregroundColor: Colors.white,
                  disabledBackgroundColor: AppTheme.surface,
                  disabledForegroundColor: AppTheme.textSub,
                ),
                icon: const Icon(Icons.stop_circle_outlined, size: 32),
                label: const Text('STOP ALARM'),
              ),
              const SizedBox(height: 24),
            ],
          ),
        ),
      ),
    );
  }
}

/// The big top status card. Three colour states:
///   red     — alarm is firing
///   green   — gateway + at least one slave online (normal armed state)
///   yellow  — gateway only (slave isn't responding to pings)
class _StatusCard extends StatelessWidget {
  final bool connected;
  final bool alarmActive;
  final bool anyNodeOnline;
  const _StatusCard({
    required this.connected,
    required this.alarmActive,
    required this.anyNodeOnline,
  });

  @override
  Widget build(BuildContext context) {
    // Dart 3 record destructuring — cute but compact.
    final (color, label, icon) = alarmActive
        ? (AppTheme.danger,  'ALARM ACTIVE',       Icons.notifications_active)
        : anyNodeOnline
            ? (AppTheme.success, 'ALL SYSTEMS ONLINE', Icons.verified)
            : (AppTheme.primary, 'GATEWAY ONLY',       Icons.link);

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppTheme.surface,
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: color, width: 2),
      ),
      child: Row(children: [
        Icon(icon, size: 48, color: color),
        const SizedBox(width: 16),
        Expanded(
          child: Text(label,
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.w700,
                color: color,
              )),
        ),
      ]),
    );
  }
}

/// One row showing a slave's online status. Green dot + "ON" when online,
/// grey + "OFF" otherwise.
class _NodeChip extends StatelessWidget {
  final int id;
  final bool online;
  const _NodeChip({required this.id, required this.online});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 18),
      decoration: BoxDecoration(
        color: AppTheme.surface,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(children: [
        Container(
          width: 14,
          height: 14,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: online ? AppTheme.success : AppTheme.textSub,
          ),
        ),
        const SizedBox(width: 10),
        Text('Hazard relay (Node $id)',
            style: const TextStyle(
                fontSize: 18, fontWeight: FontWeight.w600)),
        const Spacer(),
        Text(online ? 'ON' : 'OFF',
            style: TextStyle(
                fontSize: 16,
                color: online ? AppTheme.success : AppTheme.textSub)),
      ]),
    );
  }
}
