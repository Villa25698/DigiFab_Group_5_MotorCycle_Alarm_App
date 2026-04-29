import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import 'bluetooth/ble_service.dart';
import 'screens/dashboard_screen.dart';
import 'screens/welcome_screen.dart';
import 'screens/wobble_screen.dart';
import 'services/notification_service.dart';
import 'theme/app_theme.dart';

final GlobalKey<NavigatorState> rootNavigatorKey = GlobalKey<NavigatorState>();

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  // Lock to portrait — mounted on handlebars, rider does not rotate the phone.
  await SystemChrome.setPreferredOrientations(
      [DeviceOrientation.portraitUp, DeviceOrientation.portraitDown]);
  // Notification init must not block app startup — a plugin failure or a
  // permission dialog should not hang the UI on the splash screen.
  unawaited(NotificationService.init().catchError((e, st) {
    debugPrint('NotificationService.init failed: $e\n$st');
  }));
  runApp(const MCAlarmApp());
}

class MCAlarmApp extends StatelessWidget {
  const MCAlarmApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => BleService(),
      child: MaterialApp(
        title: 'MC Alarm',
        navigatorKey: rootNavigatorKey,
        theme: AppTheme.build(),
        debugShowCheckedModeBanner: false,
        home: const _RootRouter(),
      ),
    );
  }
}

/// Watches [BleService] for wobble events and the current alarm state.
/// Any wobble pops the emergency screen over the full navigator — regardless
/// of which screen the user is on.
class _RootRouter extends StatefulWidget {
  const _RootRouter();
  @override
  State<_RootRouter> createState() => _RootRouterState();
}

class _RootRouterState extends State<_RootRouter> {
  StreamSubscription<int>? _wobbleSub;
  bool _wobbleScreenOpen = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final ble = context.read<BleService>();
      _wobbleSub = ble.wobbleStream.listen((_) => _openWobbleScreen());
    });
  }

  Future<void> _openWobbleScreen() async {
    if (_wobbleScreenOpen) return;
    _wobbleScreenOpen = true;
    await NotificationService.fireWobbleAlert();
    await rootNavigatorKey.currentState?.push(MaterialPageRoute(
      fullscreenDialog: true,
      builder: (_) => const WobbleScreen(),
    ));
    await NotificationService.clearWobbleAlert();
    _wobbleScreenOpen = false;
  }

  @override
  void dispose() {
    _wobbleSub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleService>();
    if (ble.state == BleConnectionState.connected) {
      return const DashboardScreen();
    }
    return const WelcomeScreen();
  }
}
