#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(board, display);
#endif

#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Repeater CLI"
  #include <helpers/nrf52/EthernetCLI.h>
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

static bool echo_open = false;   // CLI echo printed, line not yet ended

#ifdef WITH_SERIAL_OBSERVER
// Serial observer: drops the packet lines while `set observer.serial off`,
// and never glues a packet line to a half-typed command's echo (the line
// parser on the other end would lose that packet).
extern "C" bool mesh_packet_log_line_start() {
  if (!the_mesh.isSerialObserverOn()) return false;
  if (echo_open) {
    Serial.print("\r\n");
    echo_open = false;
  }
  return true;
}
#endif
#ifdef ETHERNET_ENABLED
static char ethernet_command[160];
#endif

// For power saving
unsigned long POWERSAVING_FIRSTSLEEP_SECS = 300; // The first sleep (if enabled) from boot

#ifdef WITH_MQTT_UPLINK
#include <helpers/PowerSaving.h>
#ifndef UPLINK_IDLE_MS
#define UPLINK_IDLE_MS 20   // max. extra latency before a received packet is noticed
#endif
#ifndef ESP32_CPU_FREQ
#define ESP32_CPU_FREQ 80
#endif
static bool uplink_auto_sleep = false;

// PowerSaving with the WiFi uplink running. The manual light sleep of the
// plain PowerSaving build (board.sleep()) would drop the WiFi association, so
// here ESP-IDF's automatic light sleep takes over: the CPU sleeps whenever all
// tasks are idle and WiFi wakes only for the AP's DTIM beacons (modem sleep).
// The loop idles in short delays; after each one the DIO1 level is checked,
// as an edge that came during light sleep may not have reached the ISR.
static void uplinkIdle() {
  if (!uplink_auto_sleep) {
    if (PowerSaving::configurePowerManagement(ESP32_CPU_FREQ, 10, true) == ESP_OK) {
      uplink_auto_sleep = true;
    }
  }
  delay(UPLINK_IDLE_MS);
  radio_driver.pollIrqLine();
}

static void uplinkAwake() {
  if (uplink_auto_sleep) {
    PowerSaving::configurePowerManagement(ESP32_CPU_FREQ, ESP32_CPU_FREQ, false);
    uplink_auto_sleep = false;
  }
}
#endif

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
static unsigned long userBtnDownAt = 0;
#define USER_BTN_HOLD_OFF_MILLIS 1500
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // give some extra time for serial to settle so
  // boot debug messages can be seen on terminal
  delay(5000);
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

  // PowerSaving variant
#if ENV_INCLUDE_GPS == 1
  // Apply PowerSaving profile for GPS
  if (sensors.getLocationProvider() != NULL) {
    // GPS on and off duration in seconds
    sensors.getLocationProvider()->setPowerSavingProfile(the_mesh.getNodePrefs()->powersaving_enabled, 600,
                                                         86400); // Max 10 minutes, 1 day
  }
#endif

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  // PowerSaving variant
  // Added board for battery display
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION, &board);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
}

void loop() {
  // Handle Serial CLI
  int len = strlen(command);
  // `command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(command) and the loop below would then
  // index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(command)) {
    command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
      echo_open = (c != '\r');
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
    command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
    command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    echo_open = false;
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;
#ifdef ETHERNET_ENABLED
    if (!ethernet_handle_command(command, reply)) {
      the_mesh.handleCommand(0, command, reply);
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  board.loop();   // let the board feed its watchdog, run periodic housekeeping

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    if (!ethernet_handle_command(ethernet_command, reply)) {
      the_mesh.handleCommand(0, ethernet_command, reply);
    }
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_) && !defined(DISPLAY_CLASS)
  // Hold the user button to power off the SenseCAP Solar repeater.
  int btnState = digitalRead(PIN_USER_BTN);
  if (btnState == LOW) {
    if (userBtnDownAt == 0) {
      userBtnDownAt = millis();
    } else if ((unsigned long)(millis() - userBtnDownAt) >= USER_BTN_HOLD_OFF_MILLIS) {
      Serial.println("Powering off...");
      board.powerOff();  // does not return
    }
  } else {
    userBtnDownAt = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
#ifdef WITH_MQTT_UPLINK
  if (uplink.isRunning() && !the_mesh.getNodePrefs()->powersaving_enabled) {
    uplinkAwake();   // powersaving switched off at runtime
  }
#endif
  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#else
    if (the_mesh.millisHasNowPassed(POWERSAVING_FIRSTSLEEP_SECS * 1000)) { // To check if it is time to sleep
#ifdef WITH_MQTT_UPLINK
      if (uplink.isRunning()) {
        if (board.isSleepInhibited()) {
          uplinkAwake();  // OTA / button: stay awake, as board.sleep() would
        } else {
          uplinkIdle();   // automatic light sleep, WiFi stays associated
        }
      } else
#endif
      board.sleep(30); // Sleep. Wake up after a while or when receiving a LoRa packet
    }
#endif
  }

  // PowerSaving variant
  if (the_mesh.getNodePrefs()->reboot_interval > 0 &&
      the_mesh.millisHasNowPassed(the_mesh.getNodePrefs()->reboot_interval * 3600000)) {
    board.reboot();
  }
}
