#include "MqttUplink.h"

#ifdef WITH_MQTT_UPLINK

#include <WiFi.h>
#include <PubSubClient.h>
#include <Utils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <time.h>

#define UPLINK_FILE        "/uplink"
#define UPLINK_MAGIC       0x4B4C5055UL   // "UPLK"
#define UPLINK_VERSION     1
#define UPLINK_TOPIC_PACKETS 0
#define UPLINK_TOPIC_STATUS  1

#ifndef UPLINK_TASK_STACK
#define UPLINK_TASK_STACK  6144
#endif
#ifndef UPLINK_TASK_CORE
  #if CONFIG_FREERTOS_UNICORE
    #define UPLINK_TASK_CORE 0
  #else
    #define UPLINK_TASK_CORE 0     // the Arduino loop (mesh) runs on core 1
  #endif
#endif

MqttUplink uplink;

struct UplinkMsg {
  uint8_t  topic;
  uint16_t len;
  char     json[UPLINK_JSON_MAX];
};

static QueueHandle_t s_queue = NULL;
static UplinkMsg s_stage;               // mesh loop side (single task)
static UplinkMsg s_tx;                  // uplink task side
static volatile bool s_mqtt_up = false;
static volatile bool s_connect_event = false;
static volatile bool s_ntp_started = false;
static volatile uint32_t s_sent = 0;
static volatile uint32_t s_connects = 0;

static WiFiClient s_net;
static PubSubClient s_mqtt(s_net);

static const char LWT_OFFLINE[] = "{\"status\":\"offline\"}";

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

static void creds_defaults(UplinkCreds& c) {
  memset(&c, 0, sizeof(c));
  c.magic = UPLINK_MAGIC;
  c.version = UPLINK_VERSION;
  c.enabled = 0;
  c.mqtt_port = 1883;
  strcpy(c.mqtt_iata, "XXX");
}

void MqttUplink::begin(fs::FS* fs, const mesh::LocalIdentity& self_id) {
  _fs = fs;
  creds_defaults(_creds);
  if (_fs && _fs->exists(UPLINK_FILE)) {
    File f = _fs->open(UPLINK_FILE, "r");
    if (f) {
      UplinkCreds tmp;
      if (f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp) && tmp.magic == UPLINK_MAGIC &&
          tmp.version == UPLINK_VERSION) {
        _creds = tmp;
        // never trust strings read back from flash to be terminated
        _creds.wifi_ssid[sizeof(_creds.wifi_ssid) - 1] = 0;
        _creds.wifi_psk[sizeof(_creds.wifi_psk) - 1] = 0;
        _creds.mqtt_host[sizeof(_creds.mqtt_host) - 1] = 0;
        _creds.mqtt_user[sizeof(_creds.mqtt_user) - 1] = 0;
        _creds.mqtt_password[sizeof(_creds.mqtt_password) - 1] = 0;
        _creds.mqtt_iata[sizeof(_creds.mqtt_iata) - 1] = 0;
      }
      f.close();
    }
  }

  mesh::Utils::toHex(_pubkey_hex, self_id.pub_key, PUB_KEY_SIZE);
  _pubkey_hex[2 * PUB_KEY_SIZE] = 0;
  const char* iata = _creds.mqtt_iata[0] ? _creds.mqtt_iata : "XXX";
  snprintf(_packets_topic, sizeof(_packets_topic), "meshcore/%s/%s/packets", iata, _pubkey_hex);
  snprintf(_status_topic, sizeof(_status_topic), "meshcore/%s/%s/status", iata, _pubkey_hex);

  if (!_creds.enabled || !_creds.wifi_ssid[0] || !_creds.mqtt_host[0]) {
    return;   // WiFi stays off: the repeater runs exactly like the plain PowerSaving build
  }

  s_queue = xQueueCreate(UPLINK_QUEUE_LEN, sizeof(UplinkMsg));
  if (s_queue == NULL) return;

  char host[24];
  snprintf(host, sizeof(host), "meshcore-%.8s", _pubkey_hex);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(true);   // modem sleep: the radio wakes for the AP's DTIM beacons only
  WiFi.begin(_creds.wifi_ssid, _creds.wifi_psk[0] ? _creds.wifi_psk : NULL);

  if (xTaskCreatePinnedToCore(taskEntry, "uplink", UPLINK_TASK_STACK, this, 1, NULL, UPLINK_TASK_CORE) != pdPASS) {
    WiFi.mode(WIFI_OFF);
    return;
  }
  _running = true;
}

bool MqttUplink::save() {
  if (_fs == nullptr) return false;
  File f = _fs->open(UPLINK_FILE, "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t*)&_creds, sizeof(_creds)) == sizeof(_creds);
  f.close();
  return ok;
}

bool MqttUplink::isConnected() const {
  return _running && s_mqtt_up;
}

uint32_t MqttUplink::ntpTime() const {
  if (!_running || !s_ntp_started) return 0;
  time_t now = time(NULL);
  return (now > 1700000000) ? (uint32_t)now : 0;   // SNTP has answered
}

// ---------------------------------------------------------------------------
// Uplink task: WiFi/broker connection and publishing
// ---------------------------------------------------------------------------

void MqttUplink::taskEntry(void* arg) {
  ((MqttUplink*)arg)->taskLoop();
}

void MqttUplink::taskLoop() {
  uint32_t backoff_ms = 5000;
  unsigned long next_try = 0;
  char client_id[24];
  snprintf(client_id, sizeof(client_id), "meshcore_%.12s", _pubkey_hex);

  s_mqtt.setServer(_creds.mqtt_host, _creds.mqtt_port);
  s_mqtt.setBufferSize(UPLINK_JSON_MAX + 128);
  s_mqtt.setKeepAlive(60);
  s_mqtt.setSocketTimeout(5);

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      s_mqtt_up = false;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!s_ntp_started) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      s_ntp_started = true;
    }

    if (!s_mqtt.connected()) {
      s_mqtt_up = false;
      if ((long)(millis() - next_try) < 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      const char* user = _creds.mqtt_user[0] ? _creds.mqtt_user : NULL;
      const char* pass = _creds.mqtt_password[0] ? _creds.mqtt_password : NULL;
      if (s_mqtt.connect(client_id, user, pass, _status_topic, 0, true, LWT_OFFLINE)) {
        backoff_ms = 5000;
        s_connects++;
        s_mqtt_up = true;
        s_connect_event = true;   // the mesh loop queues a full "online" status
      } else {
        next_try = millis() + backoff_ms;
        if (backoff_ms < 60000) backoff_ms *= 2;
        continue;
      }
    }

    s_mqtt.loop();   // keepalive

    // Wait for the next message; blocking here lets the CPU light-sleep.
    if (xQueueReceive(s_queue, &s_tx, pdMS_TO_TICKS(1000)) == pdTRUE) {
      const char* topic = (s_tx.topic == UPLINK_TOPIC_STATUS) ? _status_topic : _packets_topic;
      bool retain = (s_tx.topic == UPLINK_TOPIC_STATUS);
      if (s_mqtt.publish(topic, (const uint8_t*)s_tx.json, s_tx.len, retain)) {
        s_sent++;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Mesh loop side
// ---------------------------------------------------------------------------

bool MqttUplink::queueJson(uint8_t topic, int len) {
  if (len <= 0 || len >= (int)sizeof(s_stage.json)) return false;
  s_stage.topic = topic;
  s_stage.len = (uint16_t)len;
  if (xQueueSend(s_queue, &s_stage, 0) != pdTRUE) {
    _dropped++;
    return false;
  }
  _queued++;
  return true;
}

void MqttUplink::queuePacket(const char* node_name, const mesh::Packet* pkt, const uint8_t* raw, int raw_len,
                             float snr, float rssi, float score, uint32_t epoch) {
  if (!_running || !s_mqtt_up || raw_len <= 0) return;   // nothing is buffered while offline

  static char raw_hex[2 * 256 + 1];
  static char name[2 * 32 + 8];
  uint8_t hash[MAX_HASH_SIZE];
  char hash_hex[2 * MAX_HASH_SIZE + 1];

  if (raw_len > 256) raw_len = 256;
  mesh::Utils::toHex(raw_hex, raw, raw_len);
  raw_hex[2 * raw_len] = 0;
  pkt->calculatePacketHash(hash);
  mesh::Utils::toHex(hash_hex, hash, MAX_HASH_SIZE);
  hash_hex[2 * MAX_HASH_SIZE] = 0;
  meshcore_json_escape(name, sizeof(name), node_name);

  MeshcorePacketJson pj = {
    epoch, name, _pubkey_hex, raw_len, (unsigned)pkt->getPayloadType(),
    pkt->isRouteDirect() ? "D" : "F", (unsigned)pkt->payload_len, raw_hex,
    (int)snr, (int)rssi, (int)(score * 1000.0f), hash_hex
  };
  queueJson(UPLINK_TOPIC_PACKETS, meshcore_build_packet_json(s_stage.json, sizeof(s_stage.json), &pj));
}

bool MqttUplink::statusDue() {
  if (!_running || !s_mqtt_up) return false;
  if (s_connect_event) {
    s_connect_event = false;
    _next_status_at = millis() + UPLINK_STATUS_INTERVAL_MS;
    return true;
  }
  if ((long)(millis() - _next_status_at) >= 0) {
    _next_status_at = millis() + UPLINK_STATUS_INTERVAL_MS;
    return true;
  }
  return false;
}

void MqttUplink::queueStatus(MeshcoreStatusJson& s) {
  if (!_running) return;
  s.origin_id = _pubkey_hex;
  if (s.status == NULL) s.status = "online";
  queueJson(UPLINK_TOPIC_STATUS, meshcore_build_status_json(s_stage.json, sizeof(s_stage.json), &s));
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

static bool set_str(char* dst, size_t cap, const char* val, char* reply, size_t rcap, const char* key) {
  if (strlen(val) >= cap) {
    snprintf(reply, rcap, "Error: %s too long (max %u)", key, (unsigned)(cap - 1));
    return false;
  }
  strcpy(dst, val);
  return true;
}

bool MqttUplink::handleCommand(const char* command, char* reply, size_t cap) {
  if (memcmp(command, "get uplink.", 11) == 0) {
    const char* key = command + 11;
    if (strcmp(key, "status") == 0) {
      snprintf(reply, cap, "> enabled=%s wifi=%s mqtt=%s rssi=%d sent=%lu queued=%lu dropped=%lu conn=%lu reboot_required=%s",
               _creds.enabled ? "yes" : "no",
               (_running && WiFi.status() == WL_CONNECTED) ? "up" : "down",
               isConnected() ? "up" : "down",
               _running ? (int)WiFi.RSSI() : 0,
               (unsigned long)s_sent, (unsigned long)_queued, (unsigned long)_dropped,
               (unsigned long)s_connects, _reboot_required ? "yes" : "no");
    } else if (strcmp(key, "enable") == 0) {
      snprintf(reply, cap, "> %s", _creds.enabled ? "on" : "off");
    } else if (strcmp(key, "wifi.ssid") == 0) {
      snprintf(reply, cap, "> %s", _creds.wifi_ssid[0] ? _creds.wifi_ssid : "(not set)");
    } else if (strcmp(key, "wifi.ip") == 0) {
      snprintf(reply, cap, "> %s", (_running && WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str() : "(not connected)");
    } else if (strcmp(key, "mqtt.host") == 0) {
      snprintf(reply, cap, "> %s", _creds.mqtt_host[0] ? _creds.mqtt_host : "(not set)");
    } else if (strcmp(key, "mqtt.port") == 0) {
      snprintf(reply, cap, "> %u", (unsigned)_creds.mqtt_port);
    } else if (strcmp(key, "mqtt.user") == 0) {
      snprintf(reply, cap, "> %s", _creds.mqtt_user[0] ? _creds.mqtt_user : "(not set)");
    } else if (strcmp(key, "mqtt.iata") == 0) {
      snprintf(reply, cap, "> %s", _creds.mqtt_iata);
    } else if (strcmp(key, "mqtt.topic") == 0) {
      snprintf(reply, cap, "> %s", _packets_topic);
    } else {
      snprintf(reply, cap, "unknown config: uplink.%s", key);
    }
    return true;
  }

  if (memcmp(command, "set uplink.", 11) == 0) {
    const char* cfg = command + 11;
    const char* val = strchr(cfg, ' ');
    if (val == NULL) {
      snprintf(reply, cap, "Error: value required");
      return true;
    }
    size_t klen = val - cfg;
    val++;
    bool ok = true;

    #define KEY_IS(k) (klen == strlen(k) && memcmp(cfg, k, klen) == 0)
    if (KEY_IS("enable")) {
      if (strcmp(val, "on") == 0) _creds.enabled = 1;
      else if (strcmp(val, "off") == 0) _creds.enabled = 0;
      else { snprintf(reply, cap, "Error: must be on or off"); return true; }
    } else if (KEY_IS("wifi.ssid")) {
      ok = set_str(_creds.wifi_ssid, sizeof(_creds.wifi_ssid), val, reply, cap, "ssid");
    } else if (KEY_IS("wifi.psk")) {
      ok = set_str(_creds.wifi_psk, sizeof(_creds.wifi_psk), val, reply, cap, "psk");
    } else if (KEY_IS("mqtt.host")) {
      ok = set_str(_creds.mqtt_host, sizeof(_creds.mqtt_host), val, reply, cap, "host");
    } else if (KEY_IS("mqtt.port")) {
      long p = atol(val);
      if (p < 1 || p > 65535) { snprintf(reply, cap, "Error: port must be 1-65535"); return true; }
      _creds.mqtt_port = (uint16_t)p;
    } else if (KEY_IS("mqtt.user")) {
      ok = set_str(_creds.mqtt_user, sizeof(_creds.mqtt_user), val, reply, cap, "user");
    } else if (KEY_IS("mqtt.password")) {
      ok = set_str(_creds.mqtt_password, sizeof(_creds.mqtt_password), val, reply, cap, "password");
    } else if (KEY_IS("mqtt.iata")) {
      ok = set_str(_creds.mqtt_iata, sizeof(_creds.mqtt_iata), val, reply, cap, "iata");
    } else {
      snprintf(reply, cap, "unknown config: uplink.%.*s", (int)klen, cfg);
      return true;
    }
    #undef KEY_IS
    if (!ok) return true;

    if (!save()) {
      snprintf(reply, cap, "Error: could not save");
      return true;
    }
    _reboot_required = true;
    snprintf(reply, cap, "OK - reboot to apply");
    return true;
  }
  return false;
}

#endif
