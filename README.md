# MC Alarm — Motorcycle hazard-detection system

Detects death-wobble events on a motorcycle, sounds a local siren, flashes
the hazard lights via wireless relay, and pops a full-screen emergency
takeover on the rider's phone.

```
 ┌────────────────────┐    BLE GATT    ┌────────────────────────┐
 │  Flutter phone app │ ─────────────► │  ESP32-C3  "master"    │
 └────────────────────┘                │  - MPU-6050 gyro       │
                                       │  - 8x8 LED matrix      │
                                       │  - 5W speaker (siren)  │
                                       │  - Test / OFF buttons  │
                                       └──────┬─────────────────┘
                                              │ ESP-NOW
                                              ▼
                                       ┌────────────────────────┐
                                       │  ESP32-C3 "slave"      │
                                       │  - 5V relay → hazards  │
                                       └────────────────────────┘
```

## Repository layout

| Folder | What it is |
| ------ | ---------- |
| [`master/`](master)             | ESP32-C3 firmware for the gateway (gyro + siren + matrix + BLE + ESP-NOW) |
| [`slave_esp2_0/`](slave_esp2_0) | ESP32-C3 firmware for the hazard-relay slave |
| [`app/`](app)                   | Flutter companion app (Android-focused) |
| [`ble_test/`](ble_test)         | Minimal BLE-only test sketch — useful when bringing up a fresh ESP32-C3 |

## Hardware

- 2× ESP32-C3 DevKitM-1
- 1× MPU-6050 6-DoF IMU (I²C)
- 1× HT16K33 8x8 LED matrix backpack (I²C, addr `0x70`)
- 1× 5V relay module → motorcycle hazard light wiring
- 1× 40 mm 4 Ω 5 W speaker
- 1× metal pushbutton (test alarm) + 1× metal pushbutton (off alarm)

## Pin map (master)

| Pin | Function |
| --- | -------- |
| GPIO 4  | Test alarm button (INPUT_PULLUP) |
| GPIO 5  | Off alarm button (INPUT_PULLUP) |
| GPIO 7  | Speaker / piezo |
| GPIO 3  | MPU-6050 SDA |
| GPIO 2  | MPU-6050 SCL |
| GPIO 18 | LED matrix SDA |
| GPIO 19 | LED matrix SCL |

## Pin map (slave)

| Pin | Function |
| --- | -------- |
| GPIO 18 | Relay (active-low) |

## Firmware quick start

1. Install the **Arduino IDE** with the **esp32 by Espressif Systems** board
   support package (core 3.x).
2. Install libraries: `Adafruit MPU6050`, `Adafruit Unified Sensor`,
   `Adafruit GFX`, `Adafruit LED Backpack`.
3. Open `master/master.ino`, select board `ESP32C3 Dev Module`, flash.
4. Open `slave_esp2_0/slave_esp2_0.ino`, set `#define NODE_ID 1`, flash.
5. The slave's MAC must match the `slave1[]` array in `master/master.ino` —
   read the MAC from the slave's Serial Monitor at boot and update the
   master if needed.

## App quick start

See [`app/README.md`](app/README.md) — it pins the exact Flutter, Android
SDK, NDK and plugin versions that work on Windows. **Use those versions or
you'll spend hours fighting toolchain bugs.**

## BLE protocol

Service UUID: `a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001`

| Char        | UUID suffix | Props        | Payload |
| ----------- | ----------- | ------------ | ------- |
| alarmState  | `…0002`     | Read, Notify | `uint8` 0/1 |
| wobbleEvent | `…0003`     | Read, Notify | `uint8` counter |
| command     | `…0004`     | Write        | `uint8` 0=stop, 1=manual, 2=toggle |
| nodeStatus  | `…0005`     | Read, Notify | `uint8` bitmask, bit n = slave n+1 online |

## Wobble flow

1. Master MPU detects 4 sign-flips of >2.5 rad/s within 600 ms.
2. Master sends a wobble notify → phone pops full-screen emergency takeover
   with a **5-second countdown**.
3. During those 5 seconds the rider can:
   - press the **OFF button** on the master, OR
   - press **STOP** in the app
   to abort. No siren, no lights, no relay.
4. If 5 seconds elapse without cancel, the master fires the siren, lights
   the hazard triangle on the matrix, and tells the slave to flash the
   hazard relay.
5. After alarm-off there's a **3-second cooldown** before wobble detection
   resumes (so the speaker's own vibration doesn't immediately re-trigger).
