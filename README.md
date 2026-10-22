# tent-light-sync

[English](README.md) | [Polski](README.pl.md)

Camping lights that make tent lines and other campsite obstacles easier to see in the dark. The goal is to help prevent trips and falls, protect camping gear from accidental damage, and create a cozy atmosphere with softly synchronized breathing lights.

Possible uses include marking:

- Tent guy lines and tie-outs.
- Hammocks and their suspension lines.
- Guy lines supporting a tent stove chimney.

## How it works

Synchronized breathing lights for ESP32 boards using ESP-NOW. Each board drives a light through a MOSFET, and nearby boards share the same brightness cycle without a router or internet connection.

The firmware is in [`sketch.ino`](sketch.ino). Flash the same sketch to every board; roles are selected automatically.

## Hardware and wiring

- An ESP32 board with Wi-Fi and a usable GPIO4 output.
- A PWM-compatible light and a power supply rated for that light.
- A logic-level N-channel MOSFET suitable for the light's voltage and current.
- A 100–220 Ω resistor between GPIO4 and the MOSFET gate.

For a typical low-side MOSFET circuit:

| Connection | Destination |
| --- | --- |
| ESP32 GPIO4 | MOSFET gate through the 100–220 Ω resistor |
| MOSFET source | Common ground |
| MOSFET drain | Light negative terminal |
| Light positive terminal | Light power supply positive |
| ESP32 GND | Light power supply ground |

Use the MOSFET to switch the light's current; do not power the light directly from GPIO4. A gate-to-ground pull-down resistor is recommended to keep the MOSFET off while the board starts; it is not controlled by the sketch.

## Upload and run

1. Install Arduino IDE and the Arduino core for your ESP32 board. The sketch uses the pin-based `ledcAttach` / `ledcWrite` API and the `esp_now_recv_info_t` receive callback, so select a core that provides these APIs.
2. Place `sketch.ino` in a sketch folder named `sketch` and open it in Arduino IDE, or accept the IDE's prompt to create the matching folder.
3. Select your ESP32 board and serial port, then upload. The sketch uses the core's `WiFi.h` and `esp_now.h` headers, plus `math.h`; no additional third-party library is referenced.
4. Open Serial Monitor at **115200 baud**.
5. Repeat for the other boards and power them within radio range.

Each board starts as a master. As boards receive broadcasts, they follow the master with the lowest advertised MAC address. With successful packet exchange, the group converges on the lowest-MAC board.

## How synchronization works

- The master broadcasts the light settings and elapsed cycle time every **3 seconds**.
- Followers copy those settings and adjust their local cycle start time on each accepted packet.
- Brightness follows a sine wave, moving smoothly from minimum to maximum and back over **10 seconds** by default.
- A follower that receives no accepted synchronization packet for **30 seconds** becomes a master and restarts its cycle. The group can then select a master again through subsequent broadcasts.
- A single board runs the same breathing effect independently.

Synchronization is periodic and does not compensate for radio latency. Role changes reset or adjust the phase, so they can cause a visible brightness jump.

## Configuration

Change these constants near the top of `sketch.ino`, then upload the updated firmware:

| Constant | Default | Meaning |
| --- | --- | --- |
| `PIN_PWM` | `4` | GPIO connected to the MOSFET gate |
| `PWM_FREQ` | `1000` | PWM frequency in Hz |
| `PWM_RES` | `10` | PWM resolution in bits; duty values range from 0 to 1023 |
| `DEFAULT_MIN_BRIGHTNESS` | `80` | Minimum PWM duty, approximately 8% |
| `DEFAULT_MAX_BRIGHTNESS` | `400` | Maximum PWM duty, approximately 39% |
| `DEFAULT_PERIOD_MS` | `10000` | Duration of one complete breathing cycle in milliseconds |
| `MASTER_SYNC_INTERVAL_MS` | `3000` | Interval between master broadcasts |
| `MASTER_TIMEOUT_MS` | `30000` | Time without accepted synchronization before becoming master |

Keep brightness values within the PWM range and the minimum no greater than the maximum. The packet stores the period in a `uint16_t`, so use a positive period no greater than 65535 ms. A received period of zero falls back to the default.

The elected master's settings are distributed to followers. Upload the same configuration to all boards if you want those settings to remain consistent after a master change.

## Packet format and limitations

`SyncPacket` is sent as a raw C++ struct containing:

| Field | Type | Purpose |
| --- | --- | --- |
| `version` | `uint8_t` | Protocol version, currently `1` |
| `mode` | `uint8_t` | Effect identifier, currently `MODE_BREATH` (`1`) |
| `master_mac` | `uint8_t[6]` | Advertised master's MAC address, used for election |
| `period_ms` | `uint16_t` | Breathing cycle duration |
| `min_brightness` | `uint16_t` | Minimum PWM duty |
| `max_brightness` | `uint16_t` | Maximum PWM duty |
| `elapsed_ms` | `uint32_t` | Time elapsed since the master's cycle start |
| `sequence` | `uint32_t` | Incrementing transmit counter, printed for diagnostics |

Receivers check packet size, version, and mode. The sequence counter is not used to reject stale packets. Because packets use the in-memory struct layout, all boards should use a compatible build and packet definition.

The sketch uses unencrypted ESP-NOW broadcasts and does not authenticate the advertised master. It sets the broadcast peer's channel to `0`, meaning the current Wi-Fi channel; it does not explicitly select a channel. Boards must operate on the same channel to communicate. Only the breathing effect is implemented.

## Serial diagnostics

| Message | Meaning |
| --- | --- |
| `OWN MAC: ...` | This board's Wi-Fi MAC address |
| `Broadcast peer added` | Broadcast peer registration succeeded |
| `ROLE: MASTER, MAC=...` | This board is generating synchronization packets |
| `ROLE: SLAVE, MASTER=... seq=...` | An accepted packet updated the follower's settings and timing |
| `SYNC TX seq=...` | ESP-NOW accepted a send request; this does not confirm reception |
| `SYNC FAIL code=...` | The send request returned an error |
| `MASTER TIMEOUT` | The follower is promoting itself after losing synchronization |

If lights do not synchronize, check radio range, matching Wi-Fi channels, compatible firmware, and the initialization messages. If a light does not respond, check the selected GPIO, common ground, MOSFET wiring, and light power supply. The sketch logs ESP-NOW initialization failures but continues running the local effect.

## Additional content

[Photos and videos — iCloud shared album](https://www.icloud.com/sharedalbum/#D1gv3nVbA1MKH-d3rlWUlG7xFMwCAEQARog94Jf2bXLYHOpxdDWDsPbc_b9ZjobRVccVwYr2VrZj9g)
