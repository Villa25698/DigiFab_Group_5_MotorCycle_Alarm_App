// =============================================================================
// MC Alarm — BLE constants shared across the Flutter app.
// =============================================================================
//
// Every UUID and command value here MUST match the firmware in master.ino.
// If you change one side, change the other or the app will silently fail
// to find the service / characteristic.
// =============================================================================

/// 128-bit UUIDs used by the master's BLE GATT service. The service UUID is
/// what we filter for during scanning, so it acts as a "MC-Alarm only" gate.
class BleUuids {
  /// The whole MC-Alarm service.
  static const String service     = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001';

  /// uint8 — 0 = alarm off, 1 = alarm on. READ + NOTIFY.
  static const String alarmState  = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0002';

  /// uint8 — wobble counter, incremented every time the master detects a
  /// wobble. The actual VALUE doesn't matter; the app reacts to CHANGES.
  /// READ + NOTIFY.
  static const String wobbleEvent = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0003';

  /// uint8 — the app writes commands here (see [BleCmd]). WRITE only.
  static const String command     = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0004';

  /// uint8 bitmask — bit n = slave (n+1) is online. READ + NOTIFY.
  static const String nodeStatus  = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0005';
}

/// Values the app writes to the [BleUuids.command] characteristic.
/// Master interprets these in CmdCallback.onWrite().
class BleCmd {
  /// Stop the alarm immediately. Also aborts a pending wobble during its
  /// 5-second grace period.
  static const int stop         = 0;

  /// Manually fire the alarm. Bypasses wobble detection — siren and lights
  /// turn on right now.
  static const int manualOn     = 1;

  /// Toggle the alarm on/off. Useful for the dashboard HAZARDS button.
  static const int toggleHazard = 2;
}

/// The advertising name the master uses. We match this OR the service UUID
/// during scanning, whichever fires first.
const String kDeviceName = 'MC-Alarm';
