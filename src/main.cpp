#include <Arduino.h>
#include <ESP8266WiFi.h>

#include <LittleFS.h>
#include <user_interface.h>

#include "AppConfig.h"
#include "RelayController.h"
#include "TimeKeeper.h"
#include "WebUi.h"
#include "WifiController.h"
#include "HolidayDb.h"
#include "HistoryLog.h"
#include "OtaUpdater.h"
#include "OverrideWindows.h"
#include "ParashaDb.h"
#include "RelayState.h"
#include "ScheduleEngine.h"
#include "StatusIndicator.h"
#include "ZmanimDb.h"

namespace {
AppConfig cfg;
WifiController wifi;
TimeKeeper timeKeeper;
RelayController relay;
WebUi web;
ZmanimDb zmanim;
HolidayDb holidays;
ParashaDb parasha;
ScheduleEngine schedule;
StatusIndicator indicator;
OtaUpdater ota;
HistoryLog history;
} // namespace

namespace {
constexpr int kWifiLedGpio = 2; // Blue LED on many ESP-12 modules
constexpr bool kWifiLedActiveLow = true;

enum class WifiLedMode : uint8_t {
  Connecting = 0,
  ApMode,
  Connected,
};

void writeWifiLed(bool on) {
  const bool level = kWifiLedActiveLow ? !on : on;
  digitalWrite(kWifiLedGpio, level ? HIGH : LOW);
}

WifiLedMode wifiLedModeNow(const WifiController &w) {
  if (WiFi.status() == WL_CONNECTED) return WifiLedMode::Connected;
  if (w.isApMode()) return WifiLedMode::ApMode;
  return WifiLedMode::Connecting;
}

bool wifiLedPatternOn(WifiLedMode mode, uint32_t elapsedMs) {
  switch (mode) {
  case WifiLedMode::Connected: {
    // One short blink every 3 seconds
    const uint32_t t = elapsedMs % 3000;
    return t < 80;
  }
  case WifiLedMode::ApMode: {
    // Double blink every 2 seconds
    const uint32_t t = elapsedMs % 2000;
    if (t < 100) return true;
    if (t < 260) return false;
    if (t < 360) return true;
    return false;
  }
  case WifiLedMode::Connecting:
  default: {
    // Fast blink ~2.5Hz
    const uint32_t t = elapsedMs % 400;
    return t < 200;
  }
  }
}

const char *wifiStatusToString(wl_status_t st) {
  switch (st) {
  case WL_NO_SHIELD:
    return "NO_SHIELD";
  case WL_IDLE_STATUS:
    return "IDLE";
  case WL_NO_SSID_AVAIL:
    return "NO_SSID_AVAIL";
  case WL_SCAN_COMPLETED:
    return "SCAN_COMPLETED";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "CONNECT_FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION_LOST";
  case WL_WRONG_PASSWORD:
    return "WRONG_PASSWORD";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  default:
    return "UNKNOWN";
  }
}

void printBootInfo() {
  Serial.printf("[boot] SmartShabat v%s\n", SHABAT_RELAY_VERSION);
  Serial.printf("[boot] mac=%s chipId=%06x\n", WiFi.macAddress().c_str(), ESP.getChipId());
}

struct ResetSeqState {
  uint32_t magic = 0;
  uint32_t count = 0;
};

constexpr uint32_t kResetSeqMagic = 0x53485253; // 'SHRS'
constexpr uint32_t kResetSeqRtcOffsetWords = 0; // 0..127, word offset
constexpr uint32_t kHardResetPresses = 5;
constexpr uint32_t kHardResetWindowMs = 15000;

bool rtcReadResetSeq(ResetSeqState &out) {
  ResetSeqState tmp{};
  const bool ok = ESP.rtcUserMemoryRead(kResetSeqRtcOffsetWords, reinterpret_cast<uint32_t *>(&tmp), sizeof(tmp));
  if (!ok) return false;
  out = tmp;
  return true;
}

void rtcWriteResetSeq(const ResetSeqState &st) {
  ResetSeqState tmp = st;
  ESP.rtcUserMemoryWrite(kResetSeqRtcOffsetWords, reinterpret_cast<uint32_t *>(&tmp), sizeof(tmp));
}

bool isExternalReset() {
  const rst_info *info = ESP.getResetInfoPtr();
  if (!info) return false;
  return info->reason == REASON_EXT_SYS_RST;
}

void doFactoryResetNow() {
  Serial.println(F("[reset] factory reset (button sequence)"));
  delay(100);
  LittleFS.format();
  WiFi.disconnect(true);
  ESP.eraseConfig();
  delay(250);
  ESP.restart();
}

void printWifiInfo(const WifiController &w) {
  const wl_status_t st = WiFi.status();
  if (w.isApMode()) {
    const String apIp = WiFi.softAPIP().toString();
    const int clients = WiFi.softAPgetStationNum();
    if (st == WL_CONNECTED) {
      Serial.printf("[net] ap ssid=%s apIp=%s clients=%d | sta ssid=%s staIp=%s rssi=%d\n",
                    w.apSsid().c_str(),
                    apIp.c_str(),
                    clients,
                    WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
      return;
    }
    Serial.printf("[net] ap ssid=%s apIp=%s clients=%d | sta=%s(%d)\n",
                  w.apSsid().c_str(),
                  apIp.c_str(),
                  clients,
                  wifiStatusToString(st),
                  static_cast<int>(st));
  } else if (st == WL_CONNECTED) {
    Serial.printf("[net] sta ssid=%s ip=%s rssi=%d\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.printf("[net] sta=%s(%d)\n", wifiStatusToString(st), static_cast<int>(st));
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println(F("SmartShabat boot"));

  // Wi‑Fi status LED (blue LED on ESP module)
  pinMode(kWifiLedGpio, OUTPUT);
  writeWifiLed(false);

  if (!LittleFS.begin()) {
    Serial.println(F("[fs] mount failed; formatting..."));
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println(F("[fs] mount failed after format"));
    }
  } else {
    Serial.println(F("[fs] mounted"));
  }

  if (!appcfg::load(cfg)) {
    appcfg::save(cfg);
  }

  // Migration: ensure OTA manifest has a sensible default so the product works out of the box,
  // even if an older config exists on LittleFS.
  if (!cfg.otaManifestUrl.length()) {
    cfg.otaManifestUrl = SHABAT_RELAY_DEFAULT_OTA_URL;
    appcfg::save(cfg);
  }

  // Product behavior: treat HTTP manifest URLs as temporary/local overrides.
  // After any reboot, revert to the built-in default OTA URL so the device is always configured normally.
  if (cfg.otaManifestUrl.startsWith("http://") && cfg.otaManifestUrl != String(SHABAT_RELAY_DEFAULT_OTA_URL)) {
    Serial.println(F("[ota] temporary manifest override detected; reverting to default"));
    cfg.otaManifestUrl = SHABAT_RELAY_DEFAULT_OTA_URL;
    appcfg::save(cfg);
  }

  // Avoid fighting over GPIO2: Wi‑Fi LED is fixed to GPIO2.
  if (cfg.statusLedGpio == kWifiLedGpio) {
    cfg.statusLedGpio = 16;
    appcfg::save(cfg);
  }

  printBootInfo();
  Serial.printf("[led] wifiGpio=%d clockGpio=%d\n", kWifiLedGpio, cfg.statusLedGpio);
  Serial.printf("[cfg] tz=UTC%+d:%02d dstMode=%u ntp=%s server=%s resync=%umin\n",
                cfg.tzOffsetMinutes / 60,
                abs(cfg.tzOffsetMinutes % 60),
                static_cast<unsigned>(cfg.dstMode),
                cfg.ntpEnabled ? "on" : "off",
                cfg.ntpServer.c_str(),
                static_cast<unsigned>(cfg.ntpResyncMinutes));
  Serial.printf("[cfg] offsets beforeShkia=%d afterMotzai=%d runMode=%u\n",
                cfg.minutesBeforeShkia,
                cfg.minutesAfterTzeit,
                static_cast<unsigned>(cfg.runMode));

  indicator.begin(cfg);
  bool lastRelayOn = false;
  const bool restored = relaystate::load(lastRelayOn);
  relay.begin(cfg, lastRelayOn);
  Serial.printf("[relay] restored=%s%s\n", lastRelayOn ? "ON" : "OFF", restored ? "" : " (default)");

  // "Hard reset" using the physical RESET button:
  // ESP8266 can't measure a long-press of RESET (CPU is held in reset), so we implement a safe sequence:
  // press RESET 5 times within ~15 seconds to factory-reset.
  //
  // Confirmation: toggle the relay 3 times before wiping.
  {
    ResetSeqState st{};
    if (!rtcReadResetSeq(st) || st.magic != kResetSeqMagic) {
      st.magic = kResetSeqMagic;
      st.count = 0;
    }

    if (isExternalReset()) {
      st.count += 1;
    } else {
      st.count = 0;
    }
    rtcWriteResetSeq(st);

    if (st.count > 0) {
      Serial.printf("[reset] extResetCount=%lu/%lu\n",
                    static_cast<unsigned long>(st.count),
                    static_cast<unsigned long>(kHardResetPresses));
    }

    if (st.count >= kHardResetPresses) {
      // Clear counter first to avoid repeating if the reset immediately restarts again.
      st.count = 0;
      rtcWriteResetSeq(st);

      const bool base = relay.isOn();
      for (uint8_t i = 0; i < 3; i += 1) {
        relay.setOn(!base);
        delay(180);
        relay.setOn(base);
        delay(180);
      }
      doFactoryResetNow();
    }
  }

  // If the clock isn't valid yet, optionally force a deterministic boot relay mode.
  // This runs only in Auto run-mode; explicit "Chol"/"Shabbat" run-modes already override behavior.
  if (!timeKeeper.isTimeValid() && cfg.runMode == 0) {
    if (cfg.relayBootMode == 1 || cfg.relayBootMode == 2) {
      const bool desiredHoly = (cfg.relayBootMode == 2);
      const bool desiredPhysical = cfg.relayHolyOnNo ? desiredHoly : !desiredHoly;
      relay.setOn(desiredPhysical);
      relaystate::save(desiredPhysical);
      Serial.printf("[relay] bootMode=%u applied\n", static_cast<unsigned>(cfg.relayBootMode));
    }
  }

  history.begin();
  history.add(0, HistoryKind::Boot, "המערכת הופעלה");

  zmanim.begin();
  holidays.begin();
  parasha.begin();
  schedule.begin(zmanim, holidays, parasha);
  ota.begin();

  wifi.begin(cfg);
  printWifiInfo(wifi);
  timeKeeper.begin(cfg);

  web.begin(cfg, wifi, timeKeeper, relay, zmanim, holidays, schedule, ota, indicator, history);
  Serial.printf("[web] url=http://%s/\n", wifi.ipString().c_str());
}

void loop() {
  // Clear the reset-sequence counter after the device has been up for a bit.
  // This forms the "time window" for the multi-press reset sequence.
  static bool resetSeqCleared = false;
  static uint32_t resetSeqStartMs = millis();
  if (!resetSeqCleared && (millis() - resetSeqStartMs) > kHardResetWindowMs) {
    ResetSeqState st{};
    if (rtcReadResetSeq(st) && st.magic == kResetSeqMagic && st.count != 0) {
      st.count = 0;
      rtcWriteResetSeq(st);
    }
    resetSeqCleared = true;
  }

  wifi.tick();
  timeKeeper.tick(cfg);

  schedule.tick(cfg, timeKeeper);
  ota.tick(cfg, timeKeeper, schedule);
  const bool timeValid = timeKeeper.isTimeValid();

  // "Holy mode" target (Shabbat/Hag) - independent from relay wiring.
  bool desiredHoly = schedule.desiredRelayOn();
  if (cfg.runMode == 1) desiredHoly = false;     // force Chol
  else if (cfg.runMode == 2) desiredHoly = true; // force Shabbat/Hag

  // Map desired mode to the physical relay (NC/NO contact mapping).
  bool baseDesired = cfg.relayHolyOnNo ? desiredHoly : !desiredHoly;

  // If the clock isn't set yet, keep the last known relay state (product behavior after power loss).
  if (!timeValid && cfg.runMode == 0) {
    if (cfg.relayBootMode == 1) {
      const bool cholHoly = false;
      baseDesired = cfg.relayHolyOnNo ? cholHoly : !cholHoly;
    } else if (cfg.relayBootMode == 2) {
      const bool shabatHoly = true;
      baseDesired = cfg.relayHolyOnNo ? shabatHoly : !shabatHoly;
    } else {
      baseDesired = relay.isOn();
    }
  }

  const uint32_t nowUtc = static_cast<uint32_t>(timeKeeper.nowUtc());
  bool desiredRelay = baseDesired;
  ActiveWindowOverride activeOv{};
  const bool windowOverrideApplied = overridesApply(cfg, nowUtc, baseDesired, desiredRelay, activeOv);

  const bool relayChanged = (desiredRelay != relay.isOn());
  if (relayChanged) {
    const uint32_t t = timeValid ? static_cast<uint32_t>(timeKeeper.nowLocal(cfg)) : 0;
    if (windowOverrideApplied && activeOv.active) {
      history.add(t, HistoryKind::Relay, desiredRelay ? "חלון ידני: הריליי הופעל" : "חלון ידני: הריליי כובה");
    } else if (cfg.runMode == 1) {
      history.add(t, HistoryKind::Relay, "מצב חול");
    } else if (cfg.runMode == 2) {
      history.add(t, HistoryKind::Relay, "מצב שבת/חג");
    } else {
      history.add(t, HistoryKind::Relay, desiredHoly ? "כניסה לשבת/חג" : "יציאה משבת/חג");
    }
  }
  relay.setOn(desiredRelay);
  if (relayChanged) {
    relaystate::save(desiredRelay);
  }

  // Heartbeat log (so you can connect a monitor any time and still see status)
  static uint32_t lastHbMs = 0;
  if (millis() - lastHbMs > 300000UL) { // 5 minutes
    lastHbMs = millis();
    const ScheduleStatus st = schedule.status();
    const time_t nowLocal = timeKeeper.isTimeValid() ? timeKeeper.nowLocal(cfg) : 0;
    tm t{};
    char buf[64] = "---- -- -- --:--";
    if (nowLocal) {
      gmtime_r(&nowLocal, &t);
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    }

    char nextBuf[64] = "—";
    if (st.ok && st.nextChangeLocal) {
      const time_t nextLocal = static_cast<time_t>(st.nextChangeLocal);
      tm nt{};
      gmtime_r(&nextLocal, &nt);
      snprintf(nextBuf, sizeof(nextBuf), "%04d-%02d-%02d %02d:%02d", nt.tm_year + 1900, nt.tm_mon + 1, nt.tm_mday, nt.tm_hour, nt.tm_min);
    }

    const String net = wifi.staSsid().length() ? ("WiFi:" + wifi.staSsid()) : (wifi.isApMode() ? ("AP:" + wifi.apSsid()) : "offline");
    Serial.printf("[state] %s | %s ip=%s relay=%s holy=%s next=%s(%s)\n",
                  buf,
                  net.c_str(),
                  wifi.ipString().c_str(),
                  relay.isOn() ? "ON" : "OFF",
                  (st.ok && st.inHolyTime) ? "yes" : "no",
                  nextBuf,
                  (st.ok && st.nextStateOn) ? "ON" : "OFF");
  }

  // Log network changes (only when something meaningful changes)
  static wl_status_t lastStaStatus = WL_IDLE_STATUS;
  static bool lastApMode = false;
  static String lastIp;
  static String lastSta;
  static String lastApSsid;
  const wl_status_t staStatus = WiFi.status();
  const bool ap = wifi.isApMode();
  const String ip = wifi.ipString();
  const String sta = wifi.staSsid();
  const String apSsid = wifi.apSsid();
  if (staStatus != lastStaStatus || ap != lastApMode || ip != lastIp || sta != lastSta || apSsid != lastApSsid) {
    printWifiInfo(wifi);
    const uint32_t t = timeKeeper.isTimeValid() ? static_cast<uint32_t>(timeKeeper.nowLocal(cfg)) : 0;
    if (staStatus == WL_CONNECTED && lastStaStatus != WL_CONNECTED) {
      history.add(t, HistoryKind::Network, "מחובר ל‑Wi‑Fi: " + WiFi.SSID());
      if (cfg.ntpEnabled && !timeKeeper.isTimeValid()) {
        timeKeeper.syncNtpNow(cfg);
      }
    } else if (lastStaStatus == WL_CONNECTED && staStatus != WL_CONNECTED) {
      history.add(t, HistoryKind::Network, "מנותק מ‑Wi‑Fi");
    }
    if (ap && !lastApMode) {
      history.add(t, HistoryKind::Network, "Hotspot פעיל: " + apSsid);
    }
    lastStaStatus = staStatus;
    lastApMode = ap;
    lastIp = ip;
    lastSta = sta;
    lastApSsid = apSsid;
  }

  // Outside-UI indication via status LED (error code = number of blinks)
  uint8_t indicatorError = 0;
  if (!timeKeeper.isTimeValid()) {
    indicatorError = StatusIndicator::kTimeInvalidCode;
  } else if (cfg.ntpEnabled) {
    const time_t lastSync = timeKeeper.lastNtpSyncUtc();
    const time_t nowUtc = timeKeeper.nowUtc();
    const bool stale =
      (cfg.ntpResyncMinutes > 0) && lastSync > 0 &&
      (nowUtc - lastSync) >= static_cast<time_t>(cfg.ntpResyncMinutes) * 60;
    if (stale) {
      indicatorError = 2;
    } else if (timeKeeper.lastNtpAttemptFailed()) {
      indicatorError = 3;
    }
  }

  // History: time source changes
  static time_t lastNtp = 0;
  static time_t lastManual = 0;
  if (timeKeeper.lastNtpSyncUtc() != 0 && timeKeeper.lastNtpSyncUtc() != lastNtp) {
    lastNtp = timeKeeper.lastNtpSyncUtc();
    const uint32_t t = timeKeeper.isTimeValid() ? static_cast<uint32_t>(timeKeeper.nowLocal(cfg)) : 0;
    history.add(t, HistoryKind::Clock, "סנכרון שעה אוטומטי");
  }
  if (timeKeeper.lastManualSetUtc() != 0 && timeKeeper.lastManualSetUtc() != lastManual) {
    lastManual = timeKeeper.lastManualSetUtc();
    const uint32_t t = timeKeeper.isTimeValid() ? static_cast<uint32_t>(timeKeeper.nowLocal(cfg)) : 0;
    history.add(t, HistoryKind::Clock, "השעון עודכן ידנית");
  }

  indicator.setErrorCode(static_cast<uint8_t>(indicatorError));
  indicator.tick();

  // Wi‑Fi LED (outside UI)
  static WifiLedMode lastWifiMode = WifiLedMode::Connecting;
  static uint32_t wifiCycleStartMs = 0;
  static bool wifiLedOn = false;
  const WifiLedMode curWifiMode = wifiLedModeNow(wifi);
  if (curWifiMode != lastWifiMode) {
    lastWifiMode = curWifiMode;
    wifiCycleStartMs = millis();
  }
  const uint32_t wifiElapsed = millis() - wifiCycleStartMs;
  const bool shouldWifiOn = wifiLedPatternOn(curWifiMode, wifiElapsed);
  if (shouldWifiOn != wifiLedOn) {
    wifiLedOn = shouldWifiOn;
    writeWifiLed(shouldWifiOn);
  }

  web.tick();
  delay(5);
}
