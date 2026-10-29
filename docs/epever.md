# Epever module (epv)

Reads an **Epever Tracer AN / AN G3** MPPT charge controller over RS-485 (Modbus RTU) and adds
its battery, PV and load values to the standard MeshCore (CayenneLPP) telemetry of a repeater or
room server, next to the I2C sensors (SHT40, INA3221, ...).

- Build flag: `ENV_INCLUDE_EPEVER=1` (see `variants/ps_epever/platformio.ini`)
- Runtime switch: `set epever on|off` (saved, default on)
- Version: `epv1.0`, shown in `ver` (e.g. `PowerSaving18-epv1.0`); see `src/helpers/ModuleVersion.h`
- Works alone or together with the observer module (`docs/observer.md`)

## Build environments

| Env | Board |
|---|---|
| `Heltec_v3_repeater_epever` | Heltec V3 |
| `heltec_v4_repeater_epever` | Heltec V4 (OLED) |
| `Heltec_t114_repeater_epever` | Heltec T114 with display |
| `Heltec_t114_without_display_repeater_epever` | Heltec T114 |
| `RAK_4631_repeater_epever` | RAK4631 |
| `RAK_3401_repeater_epever` | RAK3401 / WisMesh 1W Booster |

Builds with the observer too: `*_epever_observer` envs in `variants/ps_epever_observer`.

## Telemetry

| Channel | Content |
|---|---|
| 1 | Battery of the node, MCU temperature (as before) |
| 2.. | I2C sensors in table order (SHT40, then each INA3221 channel, ...) |
| next | Epever battery: voltage, charging current, temperature |
| next | Epever PV: voltage, current, power (1 W steps) |
| next | Epever load output: voltage, current (if the controller reports it) |

With an SHT40 and a 3-channel INA3221: 2 = SHT40, 3-5 = INA3221, 6 = battery, 7 = PV, 8 = load.
Without I2C sensors the Epever battery is channel 2. A controller that is missing, not answering
or switched off with `set epever off` is left out. Guests get the base telemetry only.

The INA3221 address is the code's default **0x42** (`TELEM_INA3221_ADDRESS`), 0.1 Ω shunts.

### Power saving

The controller is read **only when telemetry is requested** (or by `get epever`). The UART is
started for the read (about 10-50 ms) and stopped again; the transceiver's DI is held high with a
pull-up meanwhile. On nRF52 a running UARTE alone would draw ~0.9 mA and keep the HF clock on. A
reading younger than 30 s (`EPEVER_CACHE_MS`) is reused; after a failed read the next attempt
waits 5 s, so a missing controller does not block the loop on every request. On ESP32 a PM lock
keeps the chip out of light sleep for the few milliseconds of a request.

## Wiring (RS-485)

Any 3.3 V TTL-to-RS-485 module with automatic direction control (MAX3485 / SP3485 "auto
direction", or a RAK5802 on WisBlock). A module with DE/RE pins: add `-D EPEVER_PIN_DE=<pin>`.
Modules that echo the request are handled (the answer is found behind the echo by its CRC).

Controller RJ45 (COM port), per the Tracer AN manual. Check against your unit:

| RJ45 pin | Signal |
|---|---|
| 1, 2 | +5 V out. **Leave unconnected** |
| 3, 4 | RS-485 B (−) |
| 5, 6 | RS-485 A (+) |
| 7, 8 | GND, to the transceiver's GND |

If the controller does not answer, swap A and B first. If `get epever` shows `rx 0:` swap TX/RX.

| Board | UART | TX → module DI | RX ← module RO | I2C sensors |
|---|---|---|---|---|
| Heltec V3 | Serial2 | GPIO40 | GPIO41 | Wire1: GPIO6 = SDA, GPIO4 = SCL |
| Heltec V4 | Serial2 | GPIO40 | GPIO41 | Wire1: GPIO6 = SDA, GPIO4 = SCL |
| Heltec T114 | Serial2 (UARTE1) | P0.28 | P0.29 | Wire1 (board's I2C header) |
| RAK4631 | Serial1 | P0.16 (TXD1) | P0.15 (RXD1) | WisBlock I2C |
| RAK3401 / 1W Booster | Serial1 | P0.16 (TXD1) | P0.15 (RXD1) | WisBlock I2C |

On the RAK boards the RS-485 UART is the GPS slot's, so these builds have no GPS. On the Heltec
V3/V4 the OLED's I2C bus is not on the header, so the sensors are on a second bus; it needs
pull-ups to 3.3 V (most SHT40/INA3221 breakouts have them). GPIO7 is not usable on the V4.

## CLI

| Command | |
|---|---|
| `get sensors` | I2C bus and each sensor found at boot, read on the spot: `> Wire1: 4, SHT4X 21.4C 48%, INA3221/1 12.84V 0.061A 1W, ...` (`read failed` when it does not answer) |
| `get epever` | Reads the controller now: `> unit 1 polls 5 ok 5 last 0 B 13.21V 1.20A 21.5C PV 18.40V 0.90A 16W L 13.20V 0.10A`. On failure: `last <rc> rx <n>: <bytes>`; `-116` nothing came back (wiring, power), `-74` bytes but no valid answer (unit, baud, A/B), a positive value is the controller's Modbus exception code. `> off (set epever on)` when switched off. Builds without the module: `Error: no Epever support in this build` |
| `set epever on\|off` | Switches the module at runtime, saved in the prefs. Off: the bus is never touched and the channels are left out of telemetry |

## Build flags

| Flag | Default | |
|---|---|---|
| `ENV_INCLUDE_EPEVER` | – | Builds the module in |
| `EPEVER_SERIAL`, `EPEVER_PIN_TX`, `EPEVER_PIN_RX` | – | UART and pins |
| `EPEVER_PIN_DE` | – | DE/!RE pin for modules without auto direction |
| `EPEVER_BAUD` / `EPEVER_UNIT_ID` | 115200 / 1 | Tracer AN defaults |
| `EPEVER_CACHE_MS` | 30000 | Reading reuse window |
| `EPEVER_TIMEOUT_MS` | 250 | Per Modbus request |
