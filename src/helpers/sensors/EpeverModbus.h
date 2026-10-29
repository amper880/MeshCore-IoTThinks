#pragma once

// Epever Tracer AN / AN G3 charge controller over RS-485 (Modbus RTU).
//
// PowerSaving design: the controller is read only when someone asks for it
// (a telemetry request or `get epever`). The UART is started for the few
// tens of milliseconds a read takes and stopped again afterwards, so between
// requests it costs nothing (on nRF52 a running UARTE alone draws ~0.9 mA
// and keeps the HF clock on). A reading younger than EPEVER_CACHE_MS is
// reused, so a burst of requests causes one bus transaction.
//
// Build flags (per board env):
//   ENV_INCLUDE_EPEVER=1     enable
//   EPEVER_SERIAL            UART object, e.g. Serial1 / Serial2
//   EPEVER_PIN_RX            RX pin (from the module's RO)
//   EPEVER_PIN_TX            TX pin (to the module's DI)
//   EPEVER_PIN_DE            optional DE/!RE pin for modules without auto direction
//   EPEVER_BAUD              default 115200 (Tracer AN default)
//   EPEVER_UNIT_ID           default 1
//   EPEVER_CACHE_MS          default 30000
//   EPEVER_TIMEOUT_MS        per request, default 250

#if ENV_INCLUDE_EPEVER

#include <Arduino.h>
#include "EpeverProtocol.h"

#ifndef EPEVER_BAUD
#define EPEVER_BAUD 115200
#endif
#ifndef EPEVER_UNIT_ID
#define EPEVER_UNIT_ID 1
#endif
#ifndef EPEVER_CACHE_MS
#define EPEVER_CACHE_MS 30000
#endif
#ifndef EPEVER_TIMEOUT_MS
#define EPEVER_TIMEOUT_MS 250
#endif
#ifndef EPEVER_RETRY_HOLDOFF_MS
#define EPEVER_RETRY_HOLDOFF_MS 5000   // after a failed read, don't block the loop again for this long
#endif

class EpeverModbus {
public:
  // Returns true with a reading no older than EPEVER_CACHE_MS. Reads the
  // controller when the cached value is too old; `force` always reads.
  bool get(EpeverReading& out, bool force = false);

  // `get epever` reply (fits the 160-byte CLI buffer). Always reads afresh.
  void formatStatus(char* reply, size_t cap);

  int lastResult() const { return _last_rc; }

  // Runtime switch (`set epever on|off`): when off, the bus is never touched
  // and the controller's channels are left out of telemetry.
  bool isEnabled() const { return _enabled; }
  void setEnabled(bool on) { _enabled = on; if (!on) _valid = false; }

private:
  bool readController();
  int  transact(uint16_t reg, uint16_t count, uint16_t* regs);
  void portOn();
  void portOff();

  bool     _enabled = true;
  EpeverReading _reading = {};
  bool     _valid = false;
  uint32_t _read_at = 0;        // millis() of the last successful read
  uint32_t _tried_at = 0;       // millis() of the last attempt
  bool     _tried = false;
  int      _last_rc = MODBUS_ERR_TIMEOUT;
  uint32_t _polls = 0, _answered = 0;
  uint8_t  _rx[48];
  uint8_t  _rx_len = 0;
};

extern EpeverModbus epever;

#endif
