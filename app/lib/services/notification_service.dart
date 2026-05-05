// =============================================================================
// MC Alarm — Notification service
// =============================================================================
//
// Wraps flutter_local_notifications so the rest of the app can fire / clear
// the wobble emergency notification with two simple calls.
//
// Why we need a notification at all:
//   The wobble screen is pushed via the in-app navigator, which only works
//   if the app is currently in the foreground. If the rider has the phone
//   in their pocket / Spotify open / phone locked, the in-app push doesn't
//   help. A full-screen-intent notification is what wakes the screen and
//   pulls our activity to the front.
//
// Platform behaviour:
//   - Android: with USE_FULL_SCREEN_INTENT permission and our activity
//     declared with showWhenLocked + turnScreenOn, the OS launches our
//     activity on top of the lock screen. This is the same mechanism
//     incoming-call apps use.
//   - iOS: there is no equivalent. Apple does not let an app force itself
//     to the foreground. Best we can do is a critical-sound alert; the
//     rider has to tap the banner.
// =============================================================================

import 'package:flutter/material.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';

class NotificationService {
  static final _plugin = FlutterLocalNotificationsPlugin();
  static bool _initialised = false;

  /// Initialise the plugin and create the Android notification channel.
  /// Idempotent — safe to call multiple times. Called from main.dart at
  /// app start (without awaiting, so a slow init can't block the splash).
  static Future<void> init() async {
    if (_initialised) return;

    // Initialisation settings — icon for Android, permission requests for iOS.
    const android = AndroidInitializationSettings('@mipmap/ic_launcher');
    const ios = DarwinInitializationSettings(
      requestAlertPermission:    true,
      requestSoundPermission:    true,
      requestCriticalPermission: true,    // requires special Apple entitlement
    );
    await _plugin.initialize(
        const InitializationSettings(android: android, iOS: ios));

    // Android channel setup. Channels are required since Android 8.0; you
    // can only set the importance / sound / lights once per channel id.
    final androidImpl = _plugin.resolvePlatformSpecificImplementation<
        AndroidFlutterLocalNotificationsPlugin>();
    if (androidImpl != null) {
      await androidImpl.createNotificationChannel(
        const AndroidNotificationChannel(
          'wobble_emergency',
          'Speed Wobble Emergency',
          description:
              'Forces the MC Alarm app to the front when a wobble is detected.',
          importance:      Importance.max,    // heads-up + sound + vibrate
          playSound:       true,
          enableVibration: true,
          enableLights:    true,
        ),
      );
      // Android 13+ requires runtime permission for notifications.
      await androidImpl.requestNotificationsPermission();
    }

    _initialised = true;
  }

  /// Show the wobble emergency notification. fullScreenIntent + the
  /// activity flags in AndroidManifest cause Android to launch our
  /// MainActivity over the lock screen.
  static Future<void> fireWobbleAlert() async {
    const android = AndroidNotificationDetails(
      'wobble_emergency',                          // channel id
      'Speed Wobble Emergency',                    // channel name
      channelDescription: 'Emergency wobble detected',
      importance:    Importance.max,
      priority:      Priority.max,
      category:      AndroidNotificationCategory.alarm,
      fullScreenIntent: true,                      // the magic flag
      visibility:    NotificationVisibility.public,
      playSound:     true,
      enableVibration: true,
      ongoing:       true,                         // can't be swiped away
      autoCancel:    false,                        // doesn't dismiss on tap
      ticker:        'SPEED WOBBLE',
    );
    const ios = DarwinNotificationDetails(
      presentAlert: true,
      presentSound: true,
      interruptionLevel: InterruptionLevel.critical,
    );

    // Notification id 1001 is reserved for the wobble alert. Same id every
    // time so a second wobble replaces the first instead of stacking.
    await _plugin.show(
      1001,
      'SPEED WOBBLE',
      'Open the app to stop the alarm',
      const NotificationDetails(android: android, iOS: ios),
    );
  }

  /// Clear the wobble notification — called when the rider closes the
  /// emergency screen (STOP / cancelled).
  static Future<void> clearWobbleAlert() async {
    await _plugin.cancel(1001);
  }
}

/// Tiny helper for showing snackbars from places that don't want to drag
/// in the full notification import.
Future<void> showSnack(BuildContext ctx, String msg) async {
  if (!ctx.mounted) return;
  ScaffoldMessenger.of(ctx).showSnackBar(SnackBar(content: Text(msg)));
}
