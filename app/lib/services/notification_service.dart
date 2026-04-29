import 'package:flutter/material.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';

/// Fires a high-priority, full-screen-intent notification.
///
/// On Android: if the app is backgrounded, the notification is flagged as a
/// full-screen alarm and the OS promotes our WobbleActivity to the foreground
/// (requires USE_FULL_SCREEN_INTENT + android:showWhenLocked on the activity).
/// On iOS: best we can do is a critical alert — Apple does not permit forcing
/// an app to the foreground.
class NotificationService {
  static final _plugin = FlutterLocalNotificationsPlugin();
  static bool _initialised = false;

  static Future<void> init() async {
    if (_initialised) return;
    const android = AndroidInitializationSettings('@mipmap/ic_launcher');
    const ios = DarwinInitializationSettings(
      requestAlertPermission: true,
      requestSoundPermission: true,
      requestCriticalPermission: true,
    );
    await _plugin.initialize(const InitializationSettings(android: android, iOS: ios));

    final androidImpl = _plugin.resolvePlatformSpecificImplementation<
        AndroidFlutterLocalNotificationsPlugin>();
    if (androidImpl != null) {
      await androidImpl.createNotificationChannel(const AndroidNotificationChannel(
        'wobble_emergency',
        'Speed Wobble Emergency',
        description: 'Forces the MC Alarm app to the front when a wobble is detected.',
        importance: Importance.max,
        playSound: true,
        enableVibration: true,
        enableLights: true,
      ));
      await androidImpl.requestNotificationsPermission();
    }
    _initialised = true;
  }

  static Future<void> fireWobbleAlert() async {
    const android = AndroidNotificationDetails(
      'wobble_emergency',
      'Speed Wobble Emergency',
      channelDescription: 'Emergency wobble detected',
      importance: Importance.max,
      priority: Priority.max,
      category: AndroidNotificationCategory.alarm,
      fullScreenIntent: true,
      visibility: NotificationVisibility.public,
      playSound: true,
      enableVibration: true,
      ongoing: true,
      autoCancel: false,
      ticker: 'SPEED WOBBLE',
    );
    const ios = DarwinNotificationDetails(
      presentAlert: true,
      presentSound: true,
      interruptionLevel: InterruptionLevel.critical,
    );
    await _plugin.show(
      1001,
      'SPEED WOBBLE',
      'Open the app to stop the alarm',
      const NotificationDetails(android: android, iOS: ios),
    );
  }

  static Future<void> clearWobbleAlert() async {
    await _plugin.cancel(1001);
  }
}

/// Small helper for the UI — keeps the notification-service import contained.
Future<void> showSnack(BuildContext ctx, String msg) async {
  if (!ctx.mounted) return;
  ScaffoldMessenger.of(ctx).showSnackBar(SnackBar(content: Text(msg)));
}
