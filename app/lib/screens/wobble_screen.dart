import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../bluetooth/ble_service.dart';
import '../theme/app_theme.dart';

/// Full-screen emergency takeover shown when the gateway notifies us of a
/// wobble. Two controls only: toggle hazards (already on by default) and
/// STOP. Everything else is kept out of reach because the rider is
/// potentially mid-incident.
///
/// On notify the master is in a 5-second arming grace period — siren and
/// hazards have NOT fired yet. We mirror that countdown here so the rider
/// can see how much time they have to press STOP and abort.
class WobbleScreen extends StatefulWidget {
  const WobbleScreen({super.key});
  @override
  State<WobbleScreen> createState() => _WobbleScreenState();
}

class _WobbleScreenState extends State<WobbleScreen>
    with SingleTickerProviderStateMixin {
  static const int _graceSeconds = 5;

  late final AnimationController _pulse = AnimationController(
      vsync: this, duration: const Duration(milliseconds: 600))
    ..repeat(reverse: true);

  Timer? _ticker;
  int _secondsLeft = _graceSeconds;
  // True once we've left the pre-arm grace period — either because the timer
  // ran out, the master fired the alarm, or the rider pressed STOP. Once this
  // flips true it never goes back; the countdown is for the FIRST 5 seconds
  // of the screen and never again.
  bool _graceConsumed = false;

  void _endGrace() {
    if (_graceConsumed) return;
    _graceConsumed = true;
    _ticker?.cancel();
    _secondsLeft = 0;
  }

  @override
  void initState() {
    super.initState();
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
    // The moment the master flips alarmActive -> true, the grace window is
    // over. (The phone notify and the alarmState notify can land in either
    // order, so check on every rebuild.)
    if (ble.alarmActive && !_graceConsumed) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(_endGrace);
      });
    }
    final inGrace = !_graceConsumed && _secondsLeft > 0 && !ble.alarmActive;

    return PopScope(
      canPop: false, // rider can't back-out accidentally
      child: Scaffold(
        backgroundColor: Colors.black,
        body: SafeArea(
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const SizedBox(height: 8),
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
                      style: TextStyle(fontSize: 20, color: AppTheme.textSub)),
                const Spacer(),
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
                ElevatedButton(
                  onPressed: () async {
                    _endGrace();
                    await ble.stopAlarm();
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
