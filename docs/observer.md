# Packet observer module (obs)

Makes a PowerSaving repeater a **packet observer** as well:

- **ESP32** (Heltec V3, V4): every received packet is published to an MQTT broker over WiFi
  (`WITH_MQTT_UPLINK`), runtime switch `set uplink.enable on|off`.
- **nRF52** (T114, RAK4631, RAK3401): every packet is printed on the USB serial port in the
  `MESH_PACKET_LOGGING` format that meshcoretomqtt reads (`WITH_SERIAL_OBSERVER`), runtime switch
  `set observer.serial on|off`.

- Version: `obs1.0`, shown in `ver` (e.g. `PowerSaving18-obs1.0`); see `src/helpers/ModuleVersion.h`
- Works alone or together with the Epever module (`docs/epever.md`)
- **Repeater or listener only:** `set repeat off` makes the node a listener that forwards nothing
  (published as `"repeat":"off"` in the MQTT status); `set repeat on` turns forwarding back on.

## Build environments

All in `variants/ps_observer/platformio.ini`:

| Env | Board | Observer |
|---|---|---|
| `Heltec_v3_repeater_observer` | Heltec V3 | WiFi + MQTT |
| `heltec_v4_repeater_observer` | Heltec V4 (OLED) | WiFi + MQTT |
| `Heltec_t114_repeater_observer` | Heltec T114 with display | USB serial |
| `Heltec_t114_without_display_repeater_observer` | Heltec T114 | USB serial |
| `RAK_4631_repeater_observer` | RAK4631 | USB serial |
| `RAK_3401_repeater_observer` | RAK3401 / WisMesh 1W Booster | USB serial |

Builds with the Epever module too: `*_epever_observer` envs in `variants/ps_epever_observer`.

## WiFi + MQTT (Heltec V3 / V4)

Off by default. Configure over USB or the remote admin CLI, then reboot:

```
set uplink.wifi.ssid MyWiFi
set uplink.wifi.psk secret
set uplink.mqtt.host 192.168.1.10
set uplink.mqtt.port 1883
set uplink.mqtt.user meshcore          (optional)
set uplink.mqtt.password secret        (optional)
set uplink.mqtt.iata BUD
set uplink.enable on
reboot
```

Topics: `meshcore/<IATA>/<PUBKEY>/packets` (every received packet) and
`meshcore/<IATA>/<PUBKEY>/status` (retained, every 5 minutes and on connect; the broker publishes
`{"status":"offline"}` as last will). JSON is the meshcoretomqtt format;
the status carries the full version (`firmware_version`). Plain MQTT (no TLS). Packets received
while the broker is unreachable are not buffered. The clock is set from NTP once WiFi is up
(forward only).

### Power saving with WiFi

The plain PowerSaving build puts the ESP32 into manual light sleep, which drops the WiFi
association. With the uplink running and `powersaving on`, the build switches to ESP-IDF's
**automatic light sleep** after the usual 5-minute boot window: the CPU scales between 10 and
80 MHz and sleeps whenever all tasks are idle, WiFi uses modem sleep and wakes for the AP's DTIM
beacons, and the network runs in its own task, so a slow broker never blocks the mesh loop. The
loop idles in 20 ms steps and checks the radio's DIO1 level after each, so a packet edge that came
during sleep is never lost; packets are forwarded and published immediately. During OTA (or
after a button press) the node stays awake, as before. As with the plain PowerSaving build, the
USB console is only reliable in the first 5 minutes after boot (the UART/USB stops in light
sleep); later use the remote admin CLI, or `powersaving off`.

With `set uplink.enable off` (the default) the build behaves exactly like the plain PowerSaving
repeater: WiFi is never started.

## USB serial (T114, RAK4631, RAK3401)

Every packet is printed as the upstream firmware does with `MESH_PACKET_LOGGING`
(`... RAW: <hex>`, `...: RX, len=... SNR=... RSSI=... score=... time=... hash=...`,
`...: TX, ...`). A packet line never starts in the middle of a half-typed CLI command: the echo
is ended with a newline first. `set observer.serial off` silences the lines (saved in the prefs),
e.g. while working on the console.

## CLI

| Command | |
|---|---|
| `get observer.serial` | `> on` / `> off` (serial builds) |
| `set observer.serial on\|off` | Serial packet lines on or off, saved |
| `get uplink.status` | `> enabled=yes wifi=up mqtt=up rssi=-61 sent=120 queued=120 dropped=0 conn=1 reboot_required=no` |
| `get uplink.enable` / `wifi.ssid` / `wifi.ip` / `mqtt.host` / `mqtt.port` / `mqtt.user` / `mqtt.iata` / `mqtt.topic` | Saved values |
| `set uplink.enable <on\|off>` | Applies after reboot |
| `set uplink.wifi.ssid` / `wifi.psk` / `mqtt.host` / `mqtt.port` / `mqtt.user` / `mqtt.password` / `mqtt.iata` | Saved to `/uplink`, applies after reboot |

Builds without the matching part answer `Error: no serial observer in this build`, or treat
`uplink.*` as an unknown command.

## Build flags

| Flag | Default | |
|---|---|---|
| `WITH_MQTT_UPLINK` | – | WiFi + MQTT observer (ESP32) |
| `WITH_SERIAL_OBSERVER` | – | Serial observer; needs `MESH_PACKET_LOGGING=1` |
| `UPLINK_IDLE_MS` | 20 | Loop idle step in automatic light sleep |
| `UPLINK_QUEUE_LEN` | 8 | Messages waiting for the network task |
