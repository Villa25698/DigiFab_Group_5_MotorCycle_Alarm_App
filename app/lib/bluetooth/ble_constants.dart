/// UUIDs — MUST match master.ino GATT definitions.
class BleUuids {
  static const String service      = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001';
  static const String alarmState   = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0002';
  static const String wobbleEvent  = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0003';
  static const String command      = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0004';
  static const String nodeStatus   = 'a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0005';
}

/// Values written to the command characteristic.
class BleCmd {
  static const int stop        = 0;
  static const int manualOn    = 1;
  static const int toggleHazard = 2;
}

const String kDeviceName = 'MC-Alarm';
