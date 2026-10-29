# Epever module epv1.0

First release of the Epever module on PowerSaving 18. The node reports e.g.
`PowerSaving18-epv1.0` on `ver`.

- Epever Tracer AN / AN G3 charge controller over RS-485 (Modbus RTU) in telemetry: battery
  (voltage, charging current, temperature), PV (voltage, current, power) and load output
  (voltage, current), after the I2C sensors.
- Read on demand (telemetry request, `get epever`) with a 30 s cache; the UART runs only for the
  read. Works behind RS-485 transceivers that echo the request.
- `get sensors`: the I2C sensors found at boot, read on the spot.
- `get epever`: reads the controller and shows the values, or the bytes received when it fails.
- `set epever on|off`: runtime switch, saved in the prefs.
- Boards: Heltec V3, V4, T114, RAK4631, RAK3401 / WisMesh 1W Booster. Wiring: `docs/epever.md`.
