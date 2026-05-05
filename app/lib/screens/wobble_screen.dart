// =============================================================================
// MC Alarm — Wobble emergency takeover screen
// =============================================================================
//
// Pushed full-screen by main.dart's _RootRouter the moment a wobble event
// arrives over BLE. Whatever screen the rider was on (dashboard, dialog,
// even another app on Android with full-screen-intent) gets covered by this.
//
// Two controls only:
//   - HAZARD LIGHTS  (toggle — already on by default if past grace)
//   - STOP           (silences everything and pops back to the dashboard)
//
// Why so minimal?
//   The rider is potentially mid-incident. Anything more than two huge,
//   unmistakable buttons would be dangerous noise.
//
// Countdown logic:
//   When the master sends the wobble notify, it has NOT yet fired the siren —
//   it's in a 5-second arming grace period. The rider can press STOP within
//   those 5 seconds to abort. We mirror that countdown here so the rider can
//   see how long they have. After:
//     - 5 seconds elapse                  → grace consumed (alarm fires)
//     - alarmActive flips to true         → grace consumed
//     - STOP pressed                      → grace consumed
//   …the countdown is gone forever for this screen instance. (Otherwise
//   pressing STOP would weirdly bring the countdown back into view because
//   alarmActive becomes false again.)
// =============================================================================

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../bluetooth/ble_service.dart';
import '../theme/app_theme.dart';

class WobbleScreen extends StatefulWidget {
  const WobbleScreen({super.key});
  @override
  State<WobbleScreen> createState() => _WobbleScreenState();
}

class _WobbleScreenState extends State<WobbleScreen>
    with SingleTickerProviderStateMixin {
  /// Length of the master's grace period in seconds — must match
  /// WOBBLE_GRACE_MS in master.ino (5000ms).
  static const int _graceSeconds = 5;

  /// Drives the pulsing animation of the warning icon.
  late final AnimationController _pulse = AnimationController(
      vsync: this, duration: const Duration(milliseconds: 600))
    ..repeat(reverse: true);

  /// Per-second timer that decrements [_secondsLeft]. Cancelled when grace
  /// is consumed (so it doesn't keep firing in the background).
  Timer? _ticker;
  int _secondsLeft = _graceSeconds;

  /// True once we've left the pre-arm grace period for ANY reason. Once
  /// flipped true it never goes back. This guarantees the countdown stops
  /// for good and doesn't reappear if the user toggles things.
  bool _graceConsumed = false;

  /// Mark the grace period as over. Idempotent.
  void _endGrace() {
    if (_graceConsumed) return;
    _graceConsumed = true;
    _ticker?.cancel();
    _secondsLeft = 0;
  }

  @override
  void initState() {
    super.initState();
    // Strong haptic so the rider feels the alert even if the phone is
    // muted / in a glove pocket.
    HapticFeedback.heavyImpact();

    _ticker = Timer.periodic(const Duration(seconds: 1), (_) {
      if (!mounted) return;
      setState(() {
        if (_secondsLeft > 0) {
          _secondsLeft--;
          if (_secondsLeft == 0) _endGrace();
        }
      });
    });
  }

  @override
  void dispose() {
    _ticker?.cancel();
    _pulse.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleService>();

    // The wobble notify and the alarmActive notify can arrive in either
    // order over BLE. If alarmActive becomes true while we're still in
    // grace, that means the master fired the siren — end the grace.
    // Schedule via post-frame to avoid setState during build.
    if (ble.alarmActive && !_graceConsumed) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(_endGrace);
      });
    }

    final inGrace = !_graceConsumed && _secondsLeft > 0 && !ble.alarmActive;

    return PopScope(
      // Block the system back gesture / button. The rider has to use STOP.
      canPop: false,
      child: Scaffold(
        backgroundColor: Colors.black,
        body: SafeArea(
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const SizedBox(height: 8),
                // Pulsing warning icon — grabs attention.
                FadeTransition(
                  opacity: _pulse,
                  child: const Icon(Icons.warning_amber_rounded,
                      size: 120, color: AppTheme.danger),
                ),
                const SizedBox(height: 8),
                const Text('SPEED WOBBLE',
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      fontSize: 44,
                      fontWeight: FontWeight.w900,
                      color: AppTheme.danger,
                      letterSpacing: 2,
                    )),
                const SizedBox(height: 12),

                // While in grace: show countdown + "press STOP to cancel"
                // After grace: show "Hazards on — slow down safely"
                if (inGrace)
                  Column(children: [
                    Text('ALARM IN $_secondsLeft',
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                          fontSize: 36,
                          fontWeight: FontWeight.w900,
                          color: Colors.white,
                          letterSpacing: 2,
                        )),
                    const SizedBox(height: 6),
                    const Text('Press STOP to cancel',
                        textAlign: TextAlign.center,
                        style: TextStyle(
                            fontSize: 18, color: AppTheme.textSub)),
                  ])
                else
                  const Text('Hazards on — slow down safely',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                          fontSize: 20, color: AppTheme.textSub)),

                const Spacer(),

                // Hazard lights toggle. Useful if the rider survives the
                // wobble and wants to leave hazards on / off.
                ElevatedButton.icon(
                  onPressed: ble.toggleHazard,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: AppTheme.primary,
                    foregroundColor: Colors.black,
                    minimumSize: const Size.fromHeight(80),
                  ),
                  icon: const Icon(Icons.warning_amber_rounded, size: 36),
                  label: const Text('HAZARD LIGHTS',
                      style: TextStyle(
                          fontSize: 22, fontWeight: FontWeight.w800)),
                ),
                const SizedBox(height: 16),

                // STOP — the most important button on this screen.
                // Oversized on purpose. Sends cmd 0 to the master, which
                // also aborts a pending wobble during its grace period.
                ElevatedButton(
                  onPressed: () async {
                    _endGrace();                // hide countdown immediately
                    await ble.stopAlarm();      // cmd 0 -> master
                    if (context.mounted) Navigator.of(context).pop();
                  },
                  style: ElevatedButton.styleFrom(
                    backgroundColor: AppTheme.danger,
                    foregroundColor: Colors.white,
                    minimumSize: const Size.fromHeight(140),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(20)),
                  ),
                  child: const Text('STOP',
                      style: TextStyle(
                          fontSize: 44,
                          fontWeight: FontWeight.w900,
                          letterSpacing: 4)),
                ),
                const SizedBox(height: 8),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
