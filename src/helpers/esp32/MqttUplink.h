#pragma once

// WiFi + MQTT observer uplink for ESP32 repeaters (build flag WITH_MQTT_UPLINK).
//
// Every packet the node receives is published to
//   meshcore/<IATA>/<PUBKEY>/packets   (meshcoretomqtt JSON)
// and the node status to
//   meshcore/<IATA>/<PUBKEY>/status    (retained, LWT {"status":"offline"})
//
// The network side runs in its own FreeRTOS task: WiFi association, broker
// connects and TCP writes never block the mesh loop. The mesh loop only
// formats the JSON and drops it into a queue (a full queue drops the packet
// and counts it).
//
// Settings live in /uplink on the flash file system and are changed with the
// `set uplink.*` CLI commands; every change applies after a reboot.

#ifdef WITH_MQTT_UPLINK

#include <Arduino.h>
#include <FS.h>
#include <Packet.h>
#include <Identity.h>
#include <helpers/MeshcoreJson.h>

#ifndef UPLINK_JSON_MAX
#define UPLINK_JSON_MAX 1024   // a 255-byte packet is ~940 bytes of JSON
#endif
#ifndef UPLINK_QUEUE_LEN
#define UPLINK_QUEUE_LEN 8
#endif
#ifndef UPLINK_STATUS_INTERVAL_MS
#define UPLINK_STATUS_INTERVAL_MS 300000UL
#endif

struct UplinkCreds {
  uint32_t magic;
  uint8_t  version;
  uint8_t  enabled;
  uint16_t mqtt_port;
  char     wifi_ssid[33];
  char     wifi_psk[65];
  char     mqtt_host[65];
  char     mqtt_user[33];
  char     mqtt_password[65];
  char     mqtt_iata[9];
};

class MqttUplink {
public:
  // Loads the settings and, when the uplink is enabled and configured,
  // starts WiFi and the uplink task.
  void begin(fs::FS* fs, const mesh::LocalIdentity& self_id);

  // True when the uplink was started at boot (enabled + SSID + broker set).
  bool isRunning() const { return _running; }
  bool isConnected() const;

  // `get/set uplink.*`. Returns false for other commands.
  bool handleCommand(const char* command, char* reply, size_t cap);

  // Mesh loop side, never blocks.
  void queuePacket(const char* node_name, const mesh::Packet* pkt, const uint8_t* raw, int raw_len,
                   float snr, float rssi, float score, uint32_t epoch);
  void queueStatus(MeshcoreStatusJson& s);   // origin_id/status are filled in here

  // True when a status message should be queued now: after every (re)connect
  // and every UPLINK_STATUS_INTERVAL_MS while connected.
  bool statusDue();

  // Unix time from SNTP, once it is valid (0 before).
  uint32_t ntpTime() const;

private:
  static void taskEntry(void* arg);
  void taskLoop();
  bool save();
  bool queueJson(uint8_t topic, int len);

  fs::FS* _fs = nullptr;
  UplinkCreds _creds;
  bool _running = false;
  bool _reboot_required = false;
  char _pubkey_hex[2 * PUB_KEY_SIZE + 1];
  char _packets_topic[96];
  char _status_topic[96];
  unsigned long _next_status_at = 0;
  uint32_t _queued = 0, _dropped = 0;
};

extern MqttUplink uplink;

#endif
