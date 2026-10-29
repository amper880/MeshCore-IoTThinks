#pragma once

// Epever Tracer AN / AN G3 charge controller: Modbus RTU framing and register
// decoding (EPSolar "Controller Protocol V2.5"). Input registers, function
// 0x04, values x100; a 32-bit value is a low/high register pair.
//
// Platform independent (no Arduino), so the native unit tests can use it.

#include <stdint.h>
#include <stddef.h>

// 0x3100-0x3107: PV V, PV I, PV P (L, H), battery V, charging I, charging P (L, H)
#define EPEVER_REG_PV_BLOCK      0x3100
#define EPEVER_PV_BLOCK_LEN      8
// 0x310C-0x310D: load output V, I
#define EPEVER_REG_LOAD          0x310C
#define EPEVER_LOAD_LEN          2
// Battery temperature, signed
#define EPEVER_REG_BATT_TEMP     0x3110

#define MODBUS_FC_READ_INPUT     0x04

// Result codes of modbus_find_response(): 0 = OK, > 0 = Modbus exception code
// from the controller, < 0 = no usable answer (the errno values ETIMEDOUT
// and EBADMSG).
#define MODBUS_ERR_TIMEOUT       (-116)   // nothing came back (ETIMEDOUT)
#define MODBUS_ERR_BADMSG        (-74)    // bytes came back, but no valid answer (EBADMSG)

struct EpeverReading {
  float pv_voltage_v;
  float pv_current_a;
  float pv_power_w;
  float batt_voltage_v;
  float batt_charge_current_a;
  float batt_temperature_c;
  float load_voltage_v;
  float load_current_a;
  bool  has_temperature;
  bool  has_load;
};

static inline uint16_t modbus_crc16(const uint8_t* buf, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
  }
  return crc;
}

// Builds a "read input registers" request; returns its length (always 8).
static inline int modbus_build_read_input(uint8_t out[8], uint8_t unit, uint16_t reg, uint16_t count) {
  out[0] = unit;
  out[1] = MODBUS_FC_READ_INPUT;
  out[2] = (uint8_t)(reg >> 8);
  out[3] = (uint8_t)reg;
  out[4] = (uint8_t)(count >> 8);
  out[5] = (uint8_t)count;
  uint16_t crc = modbus_crc16(out, 6);
  out[6] = (uint8_t)crc;          // CRC is sent low byte first
  out[7] = (uint8_t)(crc >> 8);
  return 8;
}

// Looks for the controller's answer anywhere in `buf`. Automatic-direction
// RS-485 modules often keep their receiver on while transmitting, so the
// node's own request (and sometimes a stray byte from the bus turning
// around) arrives in front of the answer. Every candidate is checked by its
// CRC, so the echo can never be taken for an answer.
//
// Returns 0 and fills `regs` (count values) on success, the Modbus exception
// code (> 0) when the controller refused, MODBUS_ERR_TIMEOUT for an empty
// buffer and MODBUS_ERR_BADMSG when nothing valid was found.
static inline int modbus_find_response(const uint8_t* buf, size_t n, uint8_t unit,
                                       uint16_t count, uint16_t* regs) {
  if (n == 0) return MODBUS_ERR_TIMEOUT;
  const size_t want = 3 + 2 * (size_t)count + 2;

  for (size_t off = 0; off + 5 <= n; off++) {
    const uint8_t* f = &buf[off];
    if (f[0] != unit) continue;

    if (f[1] == MODBUS_FC_READ_INPUT && f[2] == 2 * count && off + want <= n) {
      uint16_t crc = modbus_crc16(f, want - 2);
      if ((uint8_t)crc == f[want - 2] && (uint8_t)(crc >> 8) == f[want - 1]) {
        for (uint16_t i = 0; i < count; i++) {
          regs[i] = (uint16_t)((f[3 + 2 * i] << 8) | f[4 + 2 * i]);
        }
        return 0;
      }
    }
    if (f[1] == (MODBUS_FC_READ_INPUT | 0x80)) {   // exception frame: 5 bytes
      uint16_t crc = modbus_crc16(f, 3);
      if ((uint8_t)crc == f[3] && (uint8_t)(crc >> 8) == f[4]) {
        return f[2] ? f[2] : MODBUS_ERR_BADMSG;
      }
    }
  }
  return MODBUS_ERR_BADMSG;
}

// True once a complete answer (or exception) is in the buffer, so the reader
// can stop waiting before its timeout.
static inline bool modbus_response_complete(const uint8_t* buf, size_t n, uint8_t unit, uint16_t count) {
  uint16_t scratch[EPEVER_PV_BLOCK_LEN];
  if (count > EPEVER_PV_BLOCK_LEN) return false;
  int rc = modbus_find_response(buf, n, unit, count, scratch);
  return rc >= 0;   // a valid answer or a valid exception
}

static inline void epever_decode_pv_block(const uint16_t r[EPEVER_PV_BLOCK_LEN], EpeverReading* out) {
  uint32_t pv_power = (uint32_t)r[2] | ((uint32_t)r[3] << 16);
  out->pv_voltage_v = r[0] / 100.0f;
  out->pv_current_a = r[1] / 100.0f;
  out->pv_power_w = (float)pv_power / 100.0f;
  out->batt_voltage_v = r[4] / 100.0f;
  out->batt_charge_current_a = r[5] / 100.0f;
}

static inline void epever_decode_temperature(uint16_t r, EpeverReading* out) {
  out->batt_temperature_c = (int16_t)r / 100.0f;
  out->has_temperature = true;
}

static inline void epever_decode_load(const uint16_t r[EPEVER_LOAD_LEN], EpeverReading* out) {
  out->load_voltage_v = r[0] / 100.0f;
  out->load_current_a = r[1] / 100.0f;
  out->has_load = true;
}
