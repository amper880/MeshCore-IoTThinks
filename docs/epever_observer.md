# PowerSaving repeater with the Epever and observer modules

This firmware has two optional modules on top of PowerSaving 18. Each can be built in alone or
together, and each can be switched off at runtime:

| Module | Build flag | Runtime switch | Version | Docs |
|---|---|---|---|---|
| Epever charge controller + I2C sensor CLI | `ENV_INCLUDE_EPEVER` | `set epever on\|off` | epv1.0 | [epever.md](epever.md) |
| Packet observer, WiFi/MQTT (ESP32) | `WITH_MQTT_UPLINK` | `set uplink.enable on\|off` | obs1.0 | [observer.md](observer.md) |
| Packet observer, USB serial (nRF52) | `WITH_SERIAL_OBSERVER` | `set observer.serial on\|off` | obs1.0 | [observer.md](observer.md) |

`ver` (and the MQTT status) report the base version followed by the modules in the build, e.g.
`PowerSaving18-epv1.0-obs1.0`. Module versions live in `src/helpers/ModuleVersion.h`, release
notes in `docs/releasenotes/`.

## Build environments

| Board | Epever only | Observer only | Both |
|---|---|---|---|
| Heltec V3 | `Heltec_v3_repeater_epever` | `Heltec_v3_repeater_observer` | `Heltec_v3_repeater_epever_observer` |
| Heltec V4 | `heltec_v4_repeater_epever` | `heltec_v4_repeater_observer` | `heltec_v4_repeater_epever_observer` |
| Heltec T114 | `Heltec_t114_repeater_epever` | `Heltec_t114_repeater_observer` | `Heltec_t114_repeater_epever_observer` |
| Heltec T114 (no display) | `Heltec_t114_without_display_repeater_epever` | `Heltec_t114_without_display_repeater_observer` | `Heltec_t114_without_display_repeater_epever_observer` |
| RAK4631 | `RAK_4631_repeater_epever` | `RAK_4631_repeater_observer` | `RAK_4631_repeater_epever_observer` |
| RAK3401 / 1W Booster | `RAK_3401_repeater_epever` | `RAK_3401_repeater_observer` | `RAK_3401_repeater_epever_observer` |

Branches: `feature/epever` and `feature/observer` carry one module each (both on
`PowerSaving-v18`), `feature/epever-observer` merges them.
