# MC Alarm — Motorcycle hazard-detection system

An accessible, retrofit-oriented warning device that detects motorcycle
speed wobble events and broadcasts a synchronised alert through three
independent channels: an LED hazard triangle on the bike, a wirelessly
switched relay that flashes the existing hazard lamps, and a full-screen
takeover on the rider's smartphone with a 5-second arming countdown.

Built for the ITI60020 *Digital Fabrication and Making* course at Østfold
University College, Spring 2026, in alignment with United Nations
Sustainable Development Goal Target 11.2 on safe, accessible, and
sustainable transport.

```
 ┌────────────────────┐    BLE GATT    ┌────────────────────────┐
 │  Flutter phone app │ ◄────────────► │  ESP32-C3  "master"    │
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
| [`master/`](master)             | ESP32-C3 firmware for the master node (gyro detection + siren + matrix + BLE + ESP-NOW) |
| [`slave_esp2_0/`](slave_esp2_0) | ESP32-C3 firmware for the hazard-relay slave |
| [`app/`](app)                   | Flutter companion app (Android) |
| [`ble_test/`](ble_test)         | Minimal BLE-only test sketch — useful when bringing up a fresh ESP32-C3 |

## Hardware

**Electronics**
- 2× ESP32-C3 DevKitM-1
- 1× InvenSense MPU-6050 6-DoF IMU (I²C, addr `0x68`)
- 1× HT16K33 8x8 LED matrix backpack (I²C, addr `0x70`)
- 1× 5V single-channel relay module → motorcycle hazard light wiring
- 1× 40 mm 4 Ω 5 W speaker
- 2× metal pushbuttons (test alarm + off alarm)

**Mechanical (digitally fabricated)**
- 3D printed PETG enclosures: master case, slave case, and two pink end-cap holders for the indicator bulbs
- 3D printed TPU strap insert (25% infill, fuzzy-skin texture) for vibration damping and grip on the handlebar
- 3D printed PETG handlebar clamp halves (two diameter variants: 25.0 mm and 35.4 mm)
- Laser-cut MDF base panels closing the enclosures
- Laser-cut acrylic mounting plate on the Segway used as a mock motorcycle test platform

## Pin map (master)

The MPU-6050 and the LED matrix share **the same I²C bus** on GPIO 2/3,
distinguished by their different I²C addresses (`0x68` and `0x70`).

| Pin     | Function                              |
| ------- | ------------------------------------- |
| GPIO 4  | Test alarm button (INPUT_PULLUP)      |
| GPIO 5  | Off alarm button (INPUT_PULLUP)       |
| GPIO 7  | Speaker / piezo                       |
| GPIO 2  | Shared I²C SDA (MPU-6050 + matrix)    |
| GPIO 3  | Shared I²C SCL (MPU-6050 + matrix)    |

## Pin map (slave)

| Pin     | Function                              |
| ------- | ------------------------------------- |
| GPIO 18 | Relay control                         |

The relay polarity is set by two named constants at the top of
[`slave_esp2_0/slave_esp2_0.ino`](slave_esp2_0/slave_esp2_0.ino):

```cpp
const int RELAY_ON  = HIGH;   // pin level that lights the hazards
const int RELAY_OFF = LOW;    // pin level that turns them off
```

If the lights are on at boot, swap the two values.

## Firmware quick start

1. Install **Arduino IDE** with the **esp32 by Espressif Systems** board
   support package (core 3.x).
2. Install Arduino libraries: `Adafruit MPU6050`, `Adafruit Unified Sensor`,
   `Adafruit GFX`, `Adafruit LED Backpack`.
3. Open [`master/master.ino`](master/master.ino), select board
   `ESP32C3 Dev Module`, flash.
4. Open [`slave_esp2_0/slave_esp2_0.ino`](slave_esp2_0/slave_esp2_0.ino),
   set `#define NODE_ID 1` (each slave needs a unique id), flash.
5. The slave's MAC must match the `slave1[]` array in `master/master.ino`.
   Read the MAC from the slave's Serial Monitor at boot and update the
   master if needed.

## App quick start

See [`app/README.md`](app/README.md) — it pins the exact Flutter, Android
SDK, NDK, and plugin versions that work on Windows. **Use those versions
or you will spend hours fighting toolchain bugs.**

## BLE protocol

Service UUID: `a8a9b000-7c1f-4d2a-9e6b-2b0f3a7c0001`

| Char        | UUID suffix | Props        | Payload                                 |
| ----------- | ----------- | ------------ | --------------------------------------- |
| alarmState  | `…0002`     | Read, Notify | `uint8` 0/1                             |
| wobbleEvent | `…0003`     | Read, Notify | `uint8` counter                         |
| command     | `…0004`     | Write        | `uint8` 0=stop, 1=manual, 2=toggle      |
| nodeStatus  | `…0005`     | Read, Notify | `uint8` bitmask, bit n = slave n+1 online |

## Wobble detection

The detector checks the strongest gyroscope axis per sample for sustained
back-and-forth motion. A trip requires **N sign reversals at >T rad/s
within W milliseconds** of each other.

| Profile     | Threshold (T) | Sign reversals (N) | Window (W) |
| ----------- | ------------- | ------------------ | ---------- |
| **Demo (default in this repo)** | 1.0 rad/s | 3 | 800 ms |
| **Production (recommended for on-bike use)** | 2.5 rad/s | 4 | 600 ms |

Demo values are easy to trigger by hand for testing. **Before mounting
on a real motorcycle, raise the values to the production profile** so road
surface vibration and engine harmonics don't cause false triggers. The
constants are at the top of `checkForWobble()` in
[`master/master.ino`](master/master.ino).

## Demo Guide
Follow these steps to power on the system and simulate motorcycle death wobble event

1. Powering on
   * Open the enclousures: Carefully open the lids of both the master(handlebar module) and the slave(light relay module)(CAUTION! Handle the Slave module (Lower box) carefullt when open, as interal components may be loose)
   * Acticate Power: Locate the battery switches and toggle them on.
   * Once the LEDs on the esp32 boards indicate power, screw the lids back on
   * Connect the 12v battery to a outlet
2. Simulating the death wobble
   * Place both hand on the handlebar grips
   * Mimic a motorcycle death wobble by making rapid left and right steering motions
   * You will have to do back and fourth motions until the LED Matric will display a hazard triangle. (It will also display on your phone if app us being used)
3. Managing the alarm
   * When the the alarm is triggered will you have a 5 second grace period to turn it off before the alarm starts. Press the Stop button on top of the master module or Stop button on your phone to turn prevent the siren and hazard light from activating
   * When the countdown reaches zero, will there be an aduble sound, matrix will keep blinking, and the hazard lights will start blinking. Press Stop on the box or phone to return the system to idle state. 

## Wobble alarm flow

1. **Idle**: master continuously samples the IMU.
2. **Detected**: wobble pattern matches the active profile → master sends
   a BLE wobble notify → phone pops the full-screen emergency takeover
   with a **5-second arming countdown**, master lights the hazard
   triangle on the LED matrix.
3. **Grace period (5 s)**: rider can abort by either:
   - pressing the **OFF button** on the master, OR
   - pressing **STOP** on the phone.
   Either action prevents the siren and the slave relay from firing.
4. **Alarm**: if 5 s elapse with no abort, the master fires the siren,
   broadcasts the alarm state to the slave over ESP-NOW, and the slave
   pulses its relay in synchrony, flashing the motorcycle's hazard
   lamps.
5. **Stop**: rider presses STOP (phone or button) → siren and relay
   silenced.
6. **Cooldown (3 s)**: wobble detection is suppressed so the speaker's own
   vibration cannot immediately re-trigger the alarm. After cooldown, the
   system returns to idle.



## Course paper

The accompanying 4-6 page extended academic abstract describes the
design rationale, related work, fabrication process, validation
results, and discussion. Submitted as part of the ITI60020 exam, May
2026.
