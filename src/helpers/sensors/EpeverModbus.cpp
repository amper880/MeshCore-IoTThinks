#include "EpeverModbus.h"

#if ENV_INCLUDE_EPEVER

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(ESP32_PLATFORM) || defined(ESP32)
  #include <sdkconfig.h>
  #if CONFIG_PM_ENABLE
    #include "esp_pm.h"
    // While a request is on the bus the ESP32 must not drop into (auto) light
    // sleep: the UART loses its clock there and the answer would be lost.
    static esp_pm_lock_handle_t s_epever_pm_lock = NULL;
  #endif
#endif

EpeverModbus epever;

void EpeverModbus::portOn() {
#if defined(ESP32_PLATFORM) || defined(ESP32)
  #if CONFIG_PM_ENABLE
  if (s_epever_pm_lock == NULL) {
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "epever", &s_epever_pm_lock);
  }
  if (s_epever_pm_lock) esp_pm_lock_acquire(s_epever_pm_lock);
  #endif
  EPEVER_SERIAL.begin(EPEVER_BAUD, SERIAL_8N1, EPEVER_PIN_RX, EPEVER_PIN_TX);
#else
  #if defined(EPEVER_PIN_RX) && defined(EPEVER_PIN_TX)
  EPEVER_SERIAL.setPins(EPEVER_PIN_RX, EPEVER_PIN_TX);
  #endif
  EPEVER_SERIAL.begin(EPEVER_BAUD);
#endif
#ifdef EPEVER_PIN_DE
  pinMode(EPEVER_PIN_DE, OUTPUT);
  digitalWrite(EPEVER_PIN_DE, LOW);   // receive
#endif
  delay(2);   // let the transceiver's line settle after the TX pin is driven again
}

void EpeverModbus::portOff() {
  EPEVER_SERIAL.end();
  // Keep the transceiver's DI high (UART idle) while the UART is off: a
  // floating DI can make an auto-direction module drive the bus.
#if defined(EPEVER_PIN_TX)
  pinMode(EPEVER_PIN_TX, INPUT_PULLUP);
#endif
#if defined(EPEVER_PIN_RX)
  pinMode(EPEVER_PIN_RX, INPUT_PULLUP);
#endif
#if defined(ESP32_PLATFORM) || defined(ESP32)
  #if CONFIG_PM_ENABLE
  if (s_epever_pm_lock) esp_pm_lock_release(s_epever_pm_lock);
  #endif
#endif
}

int EpeverModbus::transact(uint16_t reg, uint16_t count, uint16_t* regs) {
  uint8_t req[8];
  modbus_build_read_input(req, EPEVER_UNIT_ID, reg, count);

  while (EPEVER_SERIAL.available()) EPEVER_SERIAL.read();   // drop anything stale

#ifdef EPEVER_PIN_DE
  digitalWrite(EPEVER_PIN_DE, HIGH);
#endif
  EPEVER_SERIAL.write(req, sizeof(req));
  EPEVER_SERIAL.flush();   // wait until the last stop bit is out
#ifdef EPEVER_PIN_DE
  digitalWrite(EPEVER_PIN_DE, LOW);
#endif

  _rx_len = 0;
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < EPEVER_TIMEOUT_MS) {
    bool got = false;
    while (EPEVER_SERIAL.available() && _rx_len < sizeof(_rx)) {
      _rx[_rx_len++] = (uint8_t)EPEVER_SERIAL.read();
      got = true;
    }
    if (got && modbus_response_complete(_rx, _rx_len, EPEVER_UNIT_ID, count)) break;
    if (_rx_len >= sizeof(_rx)) break;
    delay(1);
  }
  return modbus_find_response(_rx, _rx_len, EPEVER_UNIT_ID, count, regs);
}

bool EpeverModbus::readController() {
  EpeverReading r = {};
  uint16_t regs[EPEVER_PV_BLOCK_LEN];

  _polls++;
  _tried = true;
  _tried_at = millis();

  portOn();
  int rc = transact(EPEVER_REG_PV_BLOCK, EPEVER_PV_BLOCK_LEN, regs);
  if (rc == 0) {
    epever_decode_pv_block(regs, &r);
    // Temperature and load output are optional: a controller that refuses
    // them still reports the battery and PV values.
    if (transact(EPEVER_REG_BATT_TEMP, 1, regs) == 0) {
      epever_decode_temperature(regs[0], &r);
    }
    if (transact(EPEVER_REG_LOAD, EPEVER_LOAD_LEN, regs) == 0) {
      epever_decode_load(regs, &r);
    }
  }
  portOff();

  _last_rc = rc;
  if (rc != 0) return false;

  _answered++;
  _reading = r;
  _valid = true;
  _read_at = millis();
  return true;
}

bool EpeverModbus::get(EpeverReading& out, bool force) {
  uint32_t now = millis();
  bool fresh = _valid && (uint32_t)(now - _read_at) < EPEVER_CACHE_MS;

  if (force || !fresh) {
    bool holdoff = _tried && _last_rc != 0 && (uint32_t)(now - _tried_at) < EPEVER_RETRY_HOLDOFF_MS;
    if (force || !holdoff) {
      readController();
    }
    fresh = _valid && (uint32_t)(millis() - _read_at) < EPEVER_CACHE_MS;
  }
  if (!fresh) return false;
  out = _reading;
  return true;
}

static int appendf(char* buf, int len, size_t cap, const char* fmt, ...) {
  if (len < 0 || (size_t)len >= cap - 1) return len;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + len, cap - len, fmt, ap);
  va_end(ap);
  if (n < 0) return len;
  return ((size_t)(len + n) < cap - 1) ? len + n : (int)cap - 1;
}

void EpeverModbus::formatStatus(char* reply, size_t cap) {
  EpeverReading r;
  bool ok = get(r, true);

  int len = appendf(reply, 0, cap, "> unit %d polls %lu ok %lu last %d",
                    EPEVER_UNIT_ID, (unsigned long)_polls, (unsigned long)_answered, _last_rc);
  if (ok) {
    len = appendf(reply, len, cap, " B %.2fV %.2fA", r.batt_voltage_v, r.batt_charge_current_a);
    if (r.has_temperature) len = appendf(reply, len, cap, " %.1fC", r.batt_temperature_c);
    len = appendf(reply, len, cap, " PV %.2fV %.2fA %.0fW", r.pv_voltage_v, r.pv_current_a, r.pv_power_w);
    if (r.has_load) len = appendf(reply, len, cap, " L %.2fV %.2fA", r.load_voltage_v, r.load_current_a);
  } else {
    // What came back for the failed request: shows a wiring/baud/unit problem.
    len = appendf(reply, len, cap, " rx %u:", (unsigned)_rx_len);
    for (uint8_t i = 0; i < _rx_len; i++) {
      len = appendf(reply, len, cap, " %02X", _rx[i]);
    }
  }
}

#endif
