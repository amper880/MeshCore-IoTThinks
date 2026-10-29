# Packet observer module obs1.0

First release of the observer module on PowerSaving 18. The node reports e.g.
`PowerSaving18-obs1.0` on `ver`.

- ESP32 (Heltec V3, V4): WiFi + MQTT uplink, meshcoretomqtt JSON on
  `meshcore/<IATA>/<PUBKEY>/packets` and `/status` (retained, LWT offline). Configured with
  `set uplink.*`, off by default.
- With PowerSaving on and the uplink running: ESP-IDF automatic light sleep and WiFi modem
  sleep instead of the manual light sleep, packets published immediately.
- nRF52 (T114, RAK4631, RAK3401): packet lines on USB serial in the `MESH_PACKET_LOGGING`
  format, never glued to a half-typed CLI echo. `set observer.serial on|off`, saved.
- `set repeat off` turns the node into a listener-only observer.
