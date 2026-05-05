// =============================================================================
// MC Alarm — Flutter app entry point
// =============================================================================
//
// This file does three things:
//   1. Boots the app — locks orientation, kicks off notification setup.
//   2. Wraps the whole app in a Provider so any widget can reach the BLE
//      service.
//   3. Watches the BLE service for "wobble detected" events. When one fires,
//      it pushes the full-screen emergency takeover (WobbleScreen) over
//      WHATEVER screen the user is on.
//
// Why "rootNavigatorKey"?
//   We need to push the wobble screen from outside any specific Navigator
//   context (so it covers the whole app, not just the current tab). A global
//   key on the MaterialApp gives us a Navigator we can drive imperatively
//   from anywhere.
// =============================================================================

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

/// Global key for the root Navigator. Lets us push routes (like the wobble
/// emergency screen) from non-widget code, e.g. a stream listener.
final GlobalKey<NavigatorState> rootNavigatorKey = GlobalKey<NavigatorState>();

Future<void> main() async {
  // Required when calling platform channel methods (orientation, plugins, …)
  // before runApp().
  WidgetsFlutterBinding.ensureInitialized();

  // The phone is meant to be mounted on the handlebars — there's no reason
  // to ever let it land in landscape. Lock to portrait both ways (so it's
  // OK if someone mounts it upside-down).
  await SystemChrome.setPreferredOrientations(
      [DeviceOrientation.portraitUp, DeviceOrientation.portraitDown]);

  // Kick off notification plugin init in the background. We deliberately
  // do NOT await it — if the plugin fails or asks for a permission dialog
  // we don't want the app to hang on the splash screen. Errors are logged
  // but not surfaced.
  unawaited(NotificationService.init().catchError((e, st) {
    debugPrint('NotificationService.init failed: $e\n$st');
  }));

  runApp(const MCAlarmApp());
}

/// Root widget. Hosts the [BleService] singleton (via Provider) and the
/// MaterialApp that everything else lives inside.
class MCAlarmApp extends StatelessWidget {
  const MCAlarmApp({super.key});

  @override
  Widget build(BuildContext context) {
    // ChangeNotifierProvider creates a BleService and disposes it when this
    // widget is destroyed. Any descendant widget can then `context.read()` /
    // `context.watch()` to get / observe it.
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

/// Decides which screen is shown at the root level:
///   - connected  → DashboardScreen
///   - otherwise  → WelcomeScreen
///
/// Also subscribes to [BleService.wobbleStream] and pops the full-screen
/// WobbleScreen the moment a wobble event arrives.
class _RootRouter extends StatefulWidget {
  const _RootRouter();
  @override
  State<_RootRouter> createState() => _RootRouterState();
}

class _RootRouterState extends State<_RootRouter> {
  /// Subscription to the BLE wobble stream. Cancelled in dispose().
  StreamSubscription<int>? _wobbleSub;

  /// Guard against pushing the wobble screen twice if a second wobble
  /// notification arrives while the screen is already up.
  bool _wobbleScreenOpen = false;

  @override
  void initState() {
    super.initState();
    // We can't call context.read() inside initState directly — the build
    // hasn't run yet. addPostFrameCallback runs after the first frame so
    // Provider's machinery is ready.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final ble = context.read<BleService>();
      _wobbleSub = ble.wobbleStream.listen((_) => _openWobbleScreen());
    });
  }

  /// Opens the full-screen emergency takeover. Also triggers an Android
  /// notification (full-screen-intent) so if the app was backgrounded it
  /// gets pulled to the foreground.
  Future<void> _openWobbleScreen() async {
    if (_wobbleScreenOpen) return;          // already showing
    _wobbleScreenOpen = true;
    await NotificationService.fireWobbleAlert();
    // Use the root navigator so the screen covers EVERYTHING (dashboards,
    // dialogs, scan screens, …).
    await rootNavigatorKey.currentState?.push(MaterialPageRoute(
      fullscreenDialog: true,
      builder: (_) => const WobbleScreen(),
    ));
    // The await above resolves when the user pops the wobble screen
    // (e.g. presses STOP). Tidy up:
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
    // watch() → rebuilds this widget whenever BleService.notifyListeners()
    // is called (i.e. the connection state changed).
    final ble = context.watch<BleService>();
    if (ble.state == BleConnectionState.connected) {
      return const DashboardScreen();
    }
    return const WelcomeScreen();
  }
}
