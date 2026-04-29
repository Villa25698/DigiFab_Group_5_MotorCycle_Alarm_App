import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../bluetooth/ble_service.dart';
import '../theme/app_theme.dart';

/// Ride-time home screen. Deliberately sparse — one status block and two
/// oversized action buttons. Anything more becomes noise to a rider glancing
/// at a handlebar-mounted phone.
class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  Future<void> _onHazardPressed(BuildContext context) async {
    final ble = context.read<BleService>();
    // Turning the alarm off via the HAZARDS button doesn't need a confirmation
    // — only ask before activating.
    if (ble.alarmActive) {
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
                connected: true,
                alarmActive: ble.alarmActive,
                anyNodeOnline: online1,
              ),
              const SizedBox(height: 16),
              _NodeChip(id: 1, online: online1),
              const Spacer(),
              ElevatedButton.icon(
                onPressed: () => _onHazardPressed(context),
                icon: const Icon(Icons.warning_amber_rounded, size: 32),
                label: Text(ble.alarmActive ? 'HAZARDS ON' : 'HAZARDS'),
              ),
              const SizedBox(height: 16),
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
    final (color, label, icon) = alarmActive
        ? (AppTheme.danger, 'ALARM ACTIVE', Icons.notifications_active)
        : anyNodeOnline
            ? (AppTheme.success, 'ALL SYSTEMS ONLINE', Icons.verified)
            : (AppTheme.primary, 'GATEWAY ONLY', Icons.link);

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
            style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w600)),
        const Spacer(),
        Text(online ? 'ON' : 'OFF',
            style: TextStyle(
                fontSize: 16,
                color: online ? AppTheme.success : AppTheme.textSub)),
      ]),
    );
  }
}
