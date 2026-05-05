# MC Alarm — Flutter companion app

Connects to the MC-Alarm ESP32-C3 gateway over BLE, shows live status, and
takes over the screen when a speed-wobble event fires.

```
 Phone (this app)
     │  BLE GATT
 ESP32-C3 "master"  ──────►  ESP-NOW  ──────► ESP32-C3 slave nodes
```

## Required toolchain (READ THIS BEFORE INSTALLING)

We hit a wall of Windows long-path / Dart compiler bugs on newer Flutter
versions. Use **exactly** these versions and you'll skip the pain.

| Tool             | Version                | Notes |
| ---------------- | ---------------------- | ----- |
| Flutter SDK      | **3.24.5 (stable)**    | Newer 3.27+ has Windows long-path bugs and a JSON-escape bug in `.flutter-plugins-dependencies` that breaks plugin discovery |
| Dart             | bundled with Flutter   | comes with 3.24.5 — don't install separately |
| Android Studio   | Hedgehog (2023.1) or newer | needed for the Android SDK + cmdline-tools, not as the editor |
| Android SDK      | API 34 / build-tools 34 | install via Android Studio's SDK Manager |
| Android NDK      | **27.0.12077973**      | required by `flutter_local_notifications` — set in `android/app/build.gradle.kts` |
| Java             | 17                     | Flutter 3.24 picks this up from Android Studio's bundled JDK |
| `flutter_blue_plus` | **1.31.17**         | 1.32+ split into platform packages with new Gradle conventions; stay on 1.31.x |
| `permission_handler` | **11.3.1**         | works cleanly with Flutter 3.24 |
| `flutter_local_notifications` | **17.2.3** | 17.0.x has a Java compile error (`bigLargeIcon ambiguous`) |

### Get Flutter 3.24.5

```bash
git clone https://github.com/flutter/flutter.git -b 3.24.5 C:\flutter
# add C:\flutter\bin to your PATH
flutter --version    # verify "Flutter 3.24.5 • channel stable"
```

If you already have a different Flutter version installed, switch with:
```bash
flutter version 3.24.5
```

### Avoid deep folder paths on Windows

Clone the repo to a short path (e.g. `C:\mc-alarm` not
`C:\Users\you\Documents\GitHub\…`). Flutter's plugin tooling chokes on
paths longer than ~150 chars on Windows.

## First-time setup

Flutter doesn't ship the platform scaffolds in source control, so generate
them once — then let the files we've committed here take over.

```bash
cd app
flutter create . --platforms=android,ios --org com.digifab --project-name mc_alarm
flutter pub get
```

`flutter create .` generates `android/`, `ios/`, `build.gradle`, etc. without
touching existing files — so `lib/`, `pubspec.yaml`, `analysis_options.yaml`
and `android/app/src/main/AndroidManifest.xml` stay as provided.

### iOS only — merge Info.plist keys

Open `ios/Runner/Info.plist` and add the keys listed in
`ios/Runner/Info.plist.additions`. They grant Bluetooth + Location
permission strings that Apple requires at App Store submission time.

### Android only — minimum SDK

Edit `android/app/build.gradle`, set:
```gradle
defaultConfig {
    minSdkVersion 23   // flutter_blue_plus requires 21+, we use 23 for safety
    targetSdkVersion 34
}
```

## Running

```bash
flutter run -d <device>           # pick a real Android phone for BLE
```

BLE does not work on the Android emulator — test on a real phone.

## GATT protocol

Service UUID: `a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001`

| Characteristic | UUID suffix | Props | Payload |
| -------------- | ----------- | ----- | ------- |
| alarmState  | `...0002` | Read, Notify | `uint8` 0/1 |
| wobbleEvent | `...0003` | Notify       | `uint8` counter |
| command     | `...0004` | Write        | `uint8` 0=stop, 1=manual, 2=toggle hazard |
| nodeStatus  | `...0005` | Read, Notify | `uint8` bitmask, bit n = slave n+1 online |

Source of truth: [master/master.ino](../master/master.ino).

## Emergency screen behaviour

When the gateway sends a `wobbleEvent` notification:

- **Foreground**: the app pushes `WobbleScreen` over the current route.
- **Background (Android)**: a high-priority `USE_FULL_SCREEN_INTENT`
  notification fires. Android promotes our activity to the foreground
  (`showWhenLocked=true`, `turnScreenOn=true` in the manifest). User can
  also tap the persistent notification.
- **Background (iOS)**: a critical-sound notification is shown. iOS does
  not permit an app to force itself to the foreground — this is a
  platform limitation, not a bug. The user must tap the banner.

The `STOP` button on the wobble screen writes `command=0` back to the
gateway, which silences the siren and clears the relays via ESP-NOW.

## Directory layout

```
lib/
├── main.dart                 App entry, root router, wobble listener
├── bluetooth/
│   ├── ble_constants.dart    UUIDs & command codes (kept in sync with firmware)
│   └── ble_service.dart      ChangeNotifier-owned BLE connection
├── services/
│   └── notification_service.dart   Full-screen-intent notification plumbing
├── screens/
│   ├── welcome_screen.dart
│   ├── permissions_screen.dart
│   ├── scan_screen.dart
│   ├── dashboard_screen.dart
│   └── wobble_screen.dart
└── theme/app_theme.dart      High-contrast, oversized-touch-target theme
```
