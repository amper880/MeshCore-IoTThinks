#pragma once

// Optional feature modules of this firmware and their versions.
//
// Each module is built in with its own flag and versioned on its own; the
// version string the node reports (`ver`, MQTT status) is the base firmware
// version followed by the versions of the modules in this build, e.g.
//   PowerSaving18-epv1.0-obs1.0
//
//   Module            Build flag                         Version
//   Epever            ENV_INCLUDE_EPEVER                 epv
//   Packet observer   WITH_MQTT_UPLINK (ESP32 WiFi/MQTT)  obs
//                     WITH_SERIAL_OBSERVER (USB serial)
//
// Release notes: docs/releasenotes/

#define MODULE_VERSION_EPEVER    "epv1.0"
#define MODULE_VERSION_OBSERVER  "obs1.0"

#if ENV_INCLUDE_EPEVER
  #define MODULE_SUFFIX_EPEVER   "-" MODULE_VERSION_EPEVER
#else
  #define MODULE_SUFFIX_EPEVER   ""
#endif

#if defined(WITH_MQTT_UPLINK) || defined(WITH_SERIAL_OBSERVER)
  #define MODULE_SUFFIX_OBSERVER "-" MODULE_VERSION_OBSERVER
#else
  #define MODULE_SUFFIX_OBSERVER ""
#endif

// FIRMWARE_VERSION must be defined before this header is included.
#define FIRMWARE_VERSION_FULL  FIRMWARE_VERSION MODULE_SUFFIX_EPEVER MODULE_SUFFIX_OBSERVER
