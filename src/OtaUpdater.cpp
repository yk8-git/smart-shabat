#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>

namespace {
constexpr const char *kStatePath = "/ota_state.json";
constexpr uint32_t kMinRetryMs = 60UL * 1000UL;

bool isHttpNetworkAvailable() {
  // Allow OTA over:
  // - STA (normal Wi‑Fi): WL_CONNECTED
  // - SoftAP (setup mode): at least one client connected to the AP
  if (WiFi.status() == WL_CONNECTED) return true;
  const WiFiMode_t mode = WiFi.getMode();
  const bool ap = (mode == WIFI_AP) || (mode == WIFI_AP_STA);
  if (!ap) return false;
  return WiFi.softAPgetStationNum() > 0;
}

String readTextFile(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) return "";
  String out;
  out.reserve(file.size());
  while (file.available()) out += static_cast<char>(file.read());
  file.close();
  return out;
}

bool writeTextFile(const char *path, const String &contents) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  const size_t written = file.print(contents);
  file.close();
  return written == contents.length();
}

String trimNotes(const String &s) {
  String out = s;
  out.trim();
  if (out.length() > 300) out = out.substring(0, 300) + "...";
  return out;
}
} // namespace

void OtaUpdater::begin() { loadState(); }

bool OtaUpdater::hasUpdateAvailable() const { return _availableVersion.length() && _availableBinUrl.length(); }

String OtaUpdater::lastError() const { return _lastError; }

bool OtaUpdater::isSafeForAutoUpdate(const ScheduleStatus &st) {
  if (!st.ok) return false;
  if (!st.hasZmanim) return false;
  if (!st.hasHolidays) return false; // conservative: don't update if we can't detect YomTov
  return !st.inHolyTime;
}

bool OtaUpdater::isBlockedByHolyTime(const ScheduleStatus &st) {
  if (!st.ok) return false; // if schedule unknown, don't block (manual may still proceed)
  return st.inHolyTime;
}

bool OtaUpdater::isHttpsUrl(const String &url) { return url.startsWith("https://"); }

static bool parseIntPart(const String &s, int &out) {
  if (!s.length()) return false;
  int value = 0;
  for (size_t i = 0; i < s.length(); i += 1) {
    const char c = s[i];
    if (!isDigit(c)) return false;
    value = value * 10 + (c - '0');
    if (value > 1000000) return false;
  }
  out = value;
  return true;
}

static bool parseSemver3(const String &raw, int &maj, int &min, int &pat) {
  String s = raw;
  s.trim();
  if (s.startsWith("v") || s.startsWith("V")) s = s.substring(1);

  const int d1 = s.indexOf('.');
  if (d1 < 0) return false;
  const int d2 = s.indexOf('.', d1 + 1);
  if (d2 < 0) return false;

  const String a = s.substring(0, d1);
  const String b = s.substring(d1 + 1, d2);

  // Patch may have suffix (e.g. 1-beta)
  String c = s.substring(d2 + 1);
  int end = 0;
  while (end < static_cast<int>(c.length()) && isDigit(c[end])) end += 1;
  c = c.substring(0, end);

  if (!parseIntPart(a, maj)) return false;
  if (!parseIntPart(b, min)) return false;
  if (!parseIntPart(c, pat)) return false;
  return true;
}

int OtaUpdater::compareVersions(const String &a, const String &b) {
  if (a == b) return 0;
  int aM = 0, am = 0, ap = 0;
  int bM = 0, bm = 0, bp = 0;
  const bool aOk = parseSemver3(a, aM, am, ap);
  const bool bOk = parseSemver3(b, bM, bm, bp);
  if (aOk && bOk) {
    if (aM != bM) return (aM < bM) ? -1 : 1;
    if (am != bm) return (am < bm) ? -1 : 1;
    if (ap != bp) return (ap < bp) ? -1 : 1;
    return 0;
  }
  // Fallback: any difference means "not equal" (treat as available if manifest differs)
  return a < b ? -1 : 1;
}

bool OtaUpdater::fetchManifest(const AppConfig &cfg,
                              String &outVersion,
                              String &outBinUrl,
                              String &outMd5,
                              String &outNotes) {
  outVersion = "";
  outBinUrl = "";
  outMd5 = "";
  outNotes = "";

  const String url = cfg.otaManifestUrl;
  if (!url.length()) {
    _lastError = "missing manifestUrl";
    return false;
  }
  if (!isHttpNetworkAvailable()) {
    _lastError = "network not connected";
    return false;
  }

  // We intentionally implement redirects ourselves instead of relying on HTTPClient's built-in
  // follow-redirects: GitHub Releases often redirects to a different HTTPS host
  // (objects.githubusercontent.com), and reusing a single TLS client can fail on ESP8266.
  String curUrl = url;
  DynamicJsonDocument doc(4096);
  bool parsed = false;
  for (uint8_t hop = 0; hop < 6; hop += 1) {
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent("shabat-relay/" + String(SHABAT_RELAY_VERSION));
    const char *hdrKeys[] = {"Location"};
    http.collectHeaders(hdrKeys, 1);

    const bool https = isHttpsUrl(curUrl);
    WiFiClient plain;
    BearSSL::WiFiClientSecure secure;
    WiFiClient *client = &plain;
    if (https) {
      secure.setInsecure(); // GitHub/raw are HTTPS; keep it simple
      // GitHub TLS handshakes can fail on ESP8266 due to RAM pressure.
      // Smaller buffers significantly improve success rates.
      secure.setBufferSizes(512, 512);
      client = &secure;
    }
    if (!http.begin(*client, curUrl)) {
      _lastError = "http begin failed";
      return false;
    }

    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      doc.clear();
      DeserializationError err = deserializeJson(doc, http.getStream());
      http.end();
      if (err) {
        _lastError = "manifest json parse failed";
        return false;
      }
      parsed = true;
      break;
    }

    // Follow common redirects (GitHub). Prefer absolute Location.
    if (code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND || code == HTTP_CODE_SEE_OTHER ||
        code == HTTP_CODE_TEMPORARY_REDIRECT || code == 308) {
      const String loc = http.header("Location");
      http.end();
      if (!loc.length()) {
        _lastError = "http " + String(code) + " redirect missing location";
        return false;
      }
      if (loc.startsWith("http://") || loc.startsWith("https://")) {
        curUrl = loc;
        continue;
      }
      _lastError = "http " + String(code) + " redirect unsupported";
      return false;
    }

    const String errStr = http.errorToString(code);
    http.end();
    if (errStr.length()) _lastError = "http " + String(code) + " " + errStr;
    else _lastError = "http " + String(code);
    return false;
  }

  if (!parsed) {
    _lastError = "http redirect limit";
    return false;
  }

  const String version = doc["version"] | "";
  const String bin = doc["bin"] | doc["url"] | "";
  const String md5 = doc["md5"] | "";
  const String notes = doc["notes"] | doc["message"] | "";

  if (!version.length() || !bin.length()) {
    _lastError = "manifest missing version/bin";
    return false;
  }

  outVersion = version;
  outBinUrl = bin;
  outMd5 = md5;
  outNotes = trimNotes(notes);
  _lastError = "";
  return true;
}

OtaCheckResult OtaUpdater::checkNow(const AppConfig &cfg) {
  OtaCheckResult r{};
  r.ok = false;
  r.available = false;

  if (!isHttpNetworkAvailable()) {
    r.message = "network not connected";
    return r;
  }
  if (!cfg.otaManifestUrl.length()) {
    r.message = "manifestUrl not set";
    return r;
  }

  String ver, bin, md5, notes;
  if (!fetchManifest(cfg, ver, bin, md5, notes)) {
    r.message = _lastError.length() ? _lastError : "manifest fetch failed";
    saveState();
    return r;
  }

  const int cmp = compareVersions(String(SHABAT_RELAY_VERSION), ver);
  const bool available = (cmp < 0);

  _availableVersion = available ? ver : "";
  _availableBinUrl = available ? bin : "";
  _availableMd5 = available ? md5 : "";
  _availableNotes = available ? notes : "";

  _lastCheckUtc = static_cast<uint32_t>(time(nullptr));
  saveState();

  r.ok = true;
  r.available = available;
  r.availableVersion = ver;
  r.message = available ? ("update available: " + ver) : "up to date";
  return r;
}

bool OtaUpdater::updateNow(const AppConfig &cfg) {
  _lastAttemptUtc = static_cast<uint32_t>(time(nullptr));
  saveState();

  if (!isHttpNetworkAvailable()) {
    _lastError = "network not connected";
    saveState();
    return false;
  }

  if (!hasUpdateAvailable()) {
    const OtaCheckResult chk = checkNow(cfg);
    if (!chk.ok) return false;
    if (!chk.available) return true; // nothing to do
  }

  ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(true);
  ESPhttpUpdate.closeConnectionsOnUpdate(true);

  if (cfg.statusLedGpio >= 0) {
    ESPhttpUpdate.setLedPin(cfg.statusLedGpio, cfg.statusLedActiveLow ? LOW : HIGH);
  }

  if (_availableMd5.length()) {
    ESPhttpUpdate.setMD5sum(_availableMd5);
  } else {
    ESPhttpUpdate.setMD5sum("");
  }

  const String url = _availableBinUrl;
  const bool https = isHttpsUrl(url);

  t_httpUpdate_return ret = HTTP_UPDATE_FAILED;
  if (https) {
    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    client.setBufferSizes(512, 512);
    ret = ESPhttpUpdate.update(client, url);
  } else {
    WiFiClient client;
    ret = ESPhttpUpdate.update(client, url);
  }

  if (ret == HTTP_UPDATE_NO_UPDATES) {
    _availableVersion = "";
    _availableBinUrl = "";
    _availableMd5 = "";
    _availableNotes = "";
    _lastError = "";
    saveState();
    return true;
  }

  if (ret == HTTP_UPDATE_OK) {
    // Typically reboots before returning.
    _lastError = "";
    saveState();
    return true;
  }

  _lastError = ESPhttpUpdate.getLastErrorString();
  saveState();
  return false;
}

void OtaUpdater::tick(const AppConfig &cfg, const TimeKeeper &time, const ScheduleEngine &schedule) {
  if (!cfg.otaAuto) return;
  if (!cfg.otaManifestUrl.length()) return;
  if (cfg.otaCheckHours == 0) return;
  if (!time.isTimeValid()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  const ScheduleStatus st = schedule.status();
  if (!isSafeForAutoUpdate(st)) return;

  const uint32_t nowUtc = static_cast<uint32_t>(time.nowUtc());
  const uint32_t interval = static_cast<uint32_t>(cfg.otaCheckHours) * 60UL * 60UL;
  if (_lastCheckUtc && (nowUtc - _lastCheckUtc) < interval) return;

  static uint32_t lastTryMs = 0;
  if (millis() - lastTryMs < kMinRetryMs) return;
  lastTryMs = millis();

  Serial.println(F("[ota] auto check"));
  const OtaCheckResult chk = checkNow(cfg);
  if (!chk.ok) {
    Serial.printf("[ota] check failed: %s\n", chk.message.c_str());
    return;
  }
  if (!chk.available) {
    Serial.println(F("[ota] up to date"));
    return;
  }

  Serial.printf("[ota] updating to %s\n", _availableVersion.c_str());
  updateNow(cfg);
}

String OtaUpdater::statusJson(const AppConfig &cfg, const TimeKeeper &time, const ScheduleEngine &schedule) const {
  DynamicJsonDocument doc(1536);
  doc["ok"] = true;
  doc["currentVersion"] = SHABAT_RELAY_VERSION;

  JsonObject c = doc.createNestedObject("config");
  c["manifestUrl"] = cfg.otaManifestUrl;
  c["auto"] = cfg.otaAuto;
  c["checkHours"] = cfg.otaCheckHours;

  const ScheduleStatus st = schedule.status();
  doc["timeValid"] = time.isTimeValid();
  doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  doc["blockedByHolyTime"] = isBlockedByHolyTime(st);

  JsonObject s = doc.createNestedObject("state");
  s["lastCheckUtc"] = _lastCheckUtc;
  s["lastAttemptUtc"] = _lastAttemptUtc;
  s["available"] = hasUpdateAvailable();
  s["availableVersion"] = _availableVersion;
  s["notes"] = _availableNotes;
  s["error"] = _lastError;

  String out;
  serializeJson(doc, out);
  return out;
}

void OtaUpdater::loadState() {
  _availableVersion = "";
  _availableBinUrl = "";
  _availableMd5 = "";
  _availableNotes = "";
  _lastError = "";
  _lastCheckUtc = 0;
  _lastAttemptUtc = 0;

  if (!LittleFS.exists(kStatePath)) return;
  const String raw = readTextFile(kStatePath);
  if (!raw.length()) return;

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, raw);
  if (err) return;

  _lastCheckUtc = doc["lastCheckUtc"] | 0;
  _lastAttemptUtc = doc["lastAttemptUtc"] | 0;
  _availableVersion = doc["availableVersion"] | "";
  _availableBinUrl = doc["availableBinUrl"] | "";
  _availableMd5 = doc["availableMd5"] | "";
  _availableNotes = doc["notes"] | "";
  _lastError = doc["error"] | "";
}

void OtaUpdater::saveState() const {
  DynamicJsonDocument doc(1536);
  doc["lastCheckUtc"] = _lastCheckUtc;
  doc["lastAttemptUtc"] = _lastAttemptUtc;
  doc["availableVersion"] = _availableVersion;
  doc["availableBinUrl"] = _availableBinUrl;
  doc["availableMd5"] = _availableMd5;
  doc["notes"] = _availableNotes;
  doc["error"] = _lastError;

  String out;
  serializeJson(doc, out);
  writeTextFile(kStatePath, out);
}
