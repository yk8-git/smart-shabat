#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#ifndef SHABAT_RELAY_DEFAULT_OTA_URL
#define SHABAT_RELAY_DEFAULT_OTA_URL "https://github.com/yk8-git/smart-shabat/releases/latest/download/ota.json"
#endif

struct ManualTimeWindow {
  uint32_t startUtc = 0;
  uint32_t endUtc = 0;
  bool on = false;
};

struct AppConfig {
  String deviceName = "ShabatRelay";

  // Network / AP
  String hostName = "";   // if empty: SmartShabat-xxxx
  bool staDhcp = true;
  IPAddress staIp = IPAddress(0, 0, 0, 0);
  IPAddress staGateway = IPAddress(0, 0, 0, 0);
  IPAddress staSubnet = IPAddress(0, 0, 0, 0);
  IPAddress staDns1 = IPAddress(0, 0, 0, 0);
  IPAddress staDns2 = IPAddress(0, 0, 0, 0);

  String apSsid = "";     // if empty: SmartShabat-xxxx
  String apPassword = ""; // <8 chars => open hotspot

  // Time
  bool ntpEnabled = true;
  String ntpServer = "pool.ntp.org";
  uint16_t ntpResyncMinutes = 360; // 0 = disable periodic resync
  int tzOffsetMinutes = 120; // UTC+2
  uint8_t dstMode = 1; // 0=off, 1=auto, 2=manual
  bool dstEnabled = true; // manual-only (dstMode=2)
  int dstOffsetMinutes = 60;

  // Location / calendar
  String locationName = "קרית שמונה";
  bool israel = true;

  // Halachic offsets
  int minutesBeforeShkia = 30;
  int minutesAfterTzeit = 30;

  // Relay
  int relayGpio = 5; // GPIO5 (D1 on many boards)
  bool relayActiveLow = true;
  // Relay contact mapping in Auto:
  // - true  => Chol = NC, Shabbat/Hag = NO (coil energized in Shabbat/Hag)
  // - false => Chol = NO, Shabbat/Hag = NC
  bool relayHolyOnNo = true;
  // Relay behavior when power returns but the clock is not valid yet (Auto mode only):
  // 0 = last physical state, 1 = force Chol, 2 = force Shabbat/Hag
  uint8_t relayBootMode = 2;

  // Status LED (outside UI indication)
  // Clock/System LED (outside UI indication). Default: GPIO16 (often a board LED / safe GPIO).
  int statusLedGpio = 16;
  bool statusLedActiveLow = true;

  // Operation
  uint8_t runMode = 0; // 0=auto, 1=weekday(always off), 2=shabbat(always on)
  static constexpr uint8_t kMaxWindows = 10;
  ManualTimeWindow windows[kMaxWindows] = {};
  uint8_t windowCount = 0;

  // OTA (GitHub/HTTP manifest-based updates)
  String otaManifestUrl = SHABAT_RELAY_DEFAULT_OTA_URL;
  bool otaAuto = false;
  uint16_t otaCheckHours = 12; // 0 = disable periodic checks
};

namespace appcfg {
bool load(AppConfig &cfg);
bool save(const AppConfig &cfg);
String toJson(const AppConfig &cfg);
bool fromJson(AppConfig &cfg, const String &json);
} // namespace appcfg
