#pragma once

// meshcoretomqtt-compatible JSON for the MQTT observer uplink: the packet and
// status messages published to meshcore/<IATA>/<PUBKEY>/packets and /status.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Copies `in` into `out` as the inside of a JSON string (quotes, backslashes
// and control characters escaped). Always NUL-terminates.
static inline void meshcore_json_escape(char* out, size_t cap, const char* in) {
  size_t o = 0;
  if (cap == 0) return;
  for (; *in && o + 1 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      if (o + 2 >= cap) break;
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c < 0x20) {
      if (o + 6 >= cap) break;
      o += snprintf(&out[o], cap - o, "\\u%04x", c);
    } else {
      out[o++] = (char)c;
    }
  }
  out[o] = 0;
}

struct MeshcorePacketJson {
  uint32_t    epoch;
  const char* origin;        // node name, already escaped
  const char* origin_id;     // public key, hex
  int         raw_len;
  unsigned    packet_type;
  const char* route;         // "F" or "D"
  unsigned    payload_len;
  const char* raw_hex;
  int         snr;
  int         rssi;
  int         score_x1000;
  const char* hash_hex;
};

static inline int meshcore_build_packet_json(char* out, size_t out_size, const MeshcorePacketJson* p) {
  struct tm tm_now;
  time_t t = (time_t)p->epoch;
  gmtime_r(&t, &tm_now);

  char ts_buf[48], time_buf[12], date_buf[32];
  snprintf(ts_buf, sizeof(ts_buf), "%04d-%02d-%02dT%02d:%02d:%02d.000000",
           tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
           tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
  snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
           tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
  snprintf(date_buf, sizeof(date_buf), "%d/%d/%04d",
           tm_now.tm_mday, tm_now.tm_mon + 1, tm_now.tm_year + 1900);

  return snprintf(out, out_size,
    "{"
    "\"type\":\"PACKET\","
    "\"origin\":\"%s\","
    "\"origin_id\":\"%s\","
    "\"timestamp\":\"%s\","
    "\"direction\":\"rx\","
    "\"time\":\"%s\","
    "\"date\":\"%s\","
    "\"len\":\"%d\","
    "\"packet_type\":\"%u\","
    "\"route\":\"%s\","
    "\"payload_len\":\"%u\","
    "\"raw\":\"%s\","
    "\"SNR\":\"%d\","
    "\"RSSI\":\"%d\","
    "\"score\":\"%d\","
    "\"hash\":\"%s\""
    "}",
    p->origin, p->origin_id, ts_buf, time_buf, date_buf,
    p->raw_len, p->packet_type, p->route, p->payload_len,
    p->raw_hex, p->snr, p->rssi, p->score_x1000, p->hash_hex);
}

struct MeshcoreStatusJson {
  const char* status;        // "online"
  uint32_t    epoch;
  const char* origin;        // escaped
  const char* origin_id;
  const char* radio;         // "freq,bw,sf,cr"
  const char* model;         // escaped
  const char* firmware_version;
  unsigned    battery_mv;
  unsigned    uptime_secs;
  unsigned    debug_flags;
  unsigned    queue_len;
  int         noise_floor;
  unsigned    tx_air_secs;
  unsigned    rx_air_secs;
  unsigned    recv_errors;
  bool        repeat;        // false: listener-only observer (`set repeat off`)
};

static inline int meshcore_build_status_json(char* out, size_t out_size, const MeshcoreStatusJson* s) {
  struct tm tm_now;
  time_t t = (time_t)s->epoch;
  gmtime_r(&t, &tm_now);

  char ts_buf[48];
  snprintf(ts_buf, sizeof(ts_buf), "%04d-%02d-%02dT%02d:%02d:%02d.000000",
           tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
           tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

  return snprintf(out, out_size,
    "{"
    "\"status\":\"%s\","
    "\"timestamp\":\"%s\","
    "\"origin\":\"%s\","
    "\"origin_id\":\"%s\","
    "\"radio\":\"%s\","
    "\"model\":\"%s\","
    "\"firmware_version\":\"%s\","
    "\"client_version\":\"meshcore-ps-uplink/1.0\","
    "\"repeat\":\"%s\","
    "\"stats\":{"
      "\"battery_mv\":%u,"
      "\"uptime_secs\":%u,"
      "\"debug_flags\":%u,"
      "\"queue_len\":%u,"
      "\"noise_floor\":%d,"
      "\"tx_air_secs\":%u,"
      "\"rx_air_secs\":%u,"
      "\"recv_errors\":%u"
    "}"
    "}",
    s->status, ts_buf, s->origin, s->origin_id, s->radio,
    s->model, s->firmware_version,
    s->repeat ? "on" : "off",
    s->battery_mv, s->uptime_secs, s->debug_flags, s->queue_len,
    s->noise_floor, s->tx_air_secs, s->rx_air_secs, s->recv_errors);
}
