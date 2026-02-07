#include "WifiController.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

namespace {
constexpr uint32_t kStaConnectTimeoutMs = 15UL * 1000UL;
constexpr uint32_t kPerSavedNetworkTimeoutMs = 12UL * 1000UL;
constexpr uint32_t kStartApAfterMs = 5UL * 1000UL;
constexpr uint32_t kStopApAfterNoClientsMs = 30UL * 1000UL;
constexpr uint32_t kPendingConnectTimeoutMs = 90UL * 1000UL;
constexpr const char *kWifiStorePath = "/wifi.json";

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
} // namespace

namespace {
String defaultSuffix4() {
  String mac = WiFi.macAddress(); // e.g. "C8:2B:96:23:02:EA"
  mac.replace(":", "");
  mac.toUpperCase();
  if (mac.length() >= 4) return mac.substring(mac.length() - 4);
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(ESP.getChipId() & 0xFFFF));
  return String(buf);
}

bool isZeroIp(const IPAddress &ip) { return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0; }
} // namespace

int WifiController::findSavedIndex(const String &ssid) const {
  for (uint8_t i = 0; i < _savedCount; i += 1) {
    if (_saved[i].ssid == ssid) return static_cast<int>(i);
  }
  return -1;
}

void WifiController::loadSaved() {
  _savedCount = 0;
  _lastSavedSsid = "";

  if (!LittleFS.exists(kWifiStorePath)) return;
  File file = LittleFS.open(kWifiStorePath, "r");
  if (!file) return;

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return;

  _lastSavedSsid = doc["last"] | "";
  JsonArray nets = doc["nets"].as<JsonArray>();
  if (nets.isNull()) return;

  for (JsonObject n : nets) {
    if (_savedCount >= kMaxSavedNetworks) break;
    const String ssid = n["ssid"] | "";
    const String password = n["password"] | "";
    if (!ssid.length()) continue;
    _saved[_savedCount].ssid = ssid;
    _saved[_savedCount].password = password;
    _savedCount += 1;
  }
}

bool WifiController::saveSaved() const {
  DynamicJsonDocument doc(2048);
  doc["last"] = _lastSavedSsid;
  JsonArray nets = doc.createNestedArray("nets");
  for (uint8_t i = 0; i < _savedCount; i += 1) {
    JsonObject n = nets.createNestedObject();
    n["ssid"] = _saved[i].ssid;
    n["password"] = _saved[i].password;
  }

  File file = LittleFS.open(kWifiStorePath, "w");
  if (!file) return false;
  const size_t written = serializeJson(doc, file);
  file.close();
  return written > 0;
}

void WifiController::rememberOnSuccess(const String &ssid, const String &password) {
  if (!ssid.length()) return;
  int idx = findSavedIndex(ssid);
  if (idx < 0) {
    if (_savedCount < kMaxSavedNetworks) {
      idx = static_cast<int>(_savedCount);
      _savedCount += 1;
    } else {
      // Drop the oldest (index 0)
      idx = 0;
      for (uint8_t i = 1; i < _savedCount; i += 1) _saved[i - 1] = _saved[i];
      idx = static_cast<int>(_savedCount - 1);
    }
  }

  _saved[idx].ssid = ssid;
  _saved[idx].password = password;
  _lastSavedSsid = ssid;
  saveSaved();
}

void WifiController::applyNetworkConfig(const AppConfig &cfg) {
  const String suffix = defaultSuffix4();
  const String fallback = "SmartShabat-" + suffix;

  _hostName = cfg.hostName.length() ? cfg.hostName : fallback;
  _staDhcp = cfg.staDhcp;
  _staIp = cfg.staIp;
  _staGateway = cfg.staGateway;
  _staSubnet = cfg.staSubnet;
  _staDns1 = cfg.staDns1;
  _staDns2 = cfg.staDns2;

  _apSsid = cfg.apSsid.length() ? cfg.apSsid : fallback;
  _apPassword = cfg.apPassword;

  WiFi.hostname(_hostName);
  if (_staDhcp) {
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  } else {
    // Only apply static config when it looks valid; otherwise fall back to DHCP.
    if (isZeroIp(_staIp) || isZeroIp(_staGateway) || isZeroIp(_staSubnet)) {
      WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    } else {
      WiFi.config(_staIp, _staGateway, _staSubnet, _staDns1, _staDns2);
    }
  }
}

void WifiController::begin(const AppConfig &cfg) {
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  applyNetworkConfig(cfg);
  _lastReconnectAttemptMs = millis();

  loadSaved();
  Serial.printf("[net] savedNetworks=%u\n", static_cast<unsigned>(_savedCount));

  // Prefer our saved list; if empty, fall back to SDK-stored creds (WiFi.begin()).
  if (_savedCount > 0) {
    bool apStarted = false;
    const uint32_t totalStart = millis();
    // Try last successful SSID first.
    int startIdx = findSavedIndex(_lastSavedSsid);
    for (uint8_t pass = 0; pass < _savedCount; pass += 1) {
      const uint8_t i = (startIdx >= 0) ? static_cast<uint8_t>((startIdx + pass) % _savedCount) : pass;
      const String ssid = _saved[i].ssid;
      const String password = _saved[i].password;
      if (!ssid.length()) continue;

      // If we already started the AP for setup, keep it running during retries.
      WiFi.mode(apStarted ? WIFI_AP_STA : WIFI_STA);
      WiFi.disconnect();
      delay(40);
      WiFi.hostname(_hostName);
      if (_staDhcp) {
        WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
      } else if (!isZeroIp(_staIp) && !isZeroIp(_staGateway) && !isZeroIp(_staSubnet)) {
        WiFi.config(_staIp, _staGateway, _staSubnet, _staDns1, _staDns2);
      }
      WiFi.begin(ssid.c_str(), password.c_str());
      Serial.printf("[net] trying ssid=%s\n", ssid.c_str());

      const uint32_t start = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - start) < kPerSavedNetworkTimeoutMs) {
        delay(120);
        yield();
        if (!apStarted && (millis() - totalStart) > kStartApAfterMs) {
          startAp();
          apStarted = true;
        }
        if (_apMode) _dns.processNextRequest();
      }

      if (WiFi.status() == WL_CONNECTED) {
        _lastStaOkMs = millis();
        _lastSavedSsid = ssid;
        saveSaved();
        Serial.printf("[net] connected ssid=%s ip=%s rssi=%d\n",
                      WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        return;
      }

      Serial.printf("[net] failed ssid=%s status=%s(%d)\n",
                    ssid.c_str(),
                    wifiStatusToString(WiFi.status()),
                    static_cast<int>(WiFi.status()));
    }

    if (!apStarted) startAp();

    // Keep trying the last known-good SSID in the background (auto-reconnect),
    // so if the router appears later we still have a good target.
    const int lastIdx = findSavedIndex(_lastSavedSsid);
    if (lastIdx >= 0) {
      const String ssid = _saved[lastIdx].ssid;
      const String password = _saved[lastIdx].password;
      if (ssid.length()) {
        WiFi.hostname(_hostName);
        if (_staDhcp) {
          WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
        } else if (!isZeroIp(_staIp) && !isZeroIp(_staGateway) && !isZeroIp(_staSubnet)) {
          WiFi.config(_staIp, _staGateway, _staSubnet, _staDns1, _staDns2);
        }
        WiFi.begin(ssid.c_str(), password.c_str());
        Serial.printf("[net] background retry ssid=%s\n", ssid.c_str());
      }
    }
  } else {
    WiFi.hostname(_hostName);
    if (_staDhcp) {
      WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    } else if (!isZeroIp(_staIp) && !isZeroIp(_staGateway) && !isZeroIp(_staSubnet)) {
      WiFi.config(_staIp, _staGateway, _staSubnet, _staDns1, _staDns2);
    }
    WiFi.begin(); // last creds from SDK (if any)
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < kStaConnectTimeoutMs) {
      delay(100);
      yield();
      // If connection isn't immediate, start the AP so setup is always possible.
      if (!_apMode && (millis() - start) > kStartApAfterMs) {
        startAp();
      }
      if (_apMode) _dns.processNextRequest();
    }
    if (WiFi.status() == WL_CONNECTED) {
      _apMode = false;
      _lastStaOkMs = millis();
      Serial.printf("[net] connected(ssdk) ssid=%s ip=%s rssi=%d\n",
                    WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
      return;
    }
  }

  startAp();
}

void WifiController::tick() {
  // If a user initiated a connect request, it may complete asynchronously.
  if (_pendingActive) {
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED && WiFi.SSID() == _pendingSsid) {
      const IPAddress ip = WiFi.localIP();
      if (!(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0)) {
        _lastStaOkMs = millis();
        rememberOnSuccess(_pendingSsid, _pendingPassword);
        Serial.printf("[net] connect completed ssid=%s ip=%s rssi=%d\n",
                      WiFi.SSID().c_str(),
                      ip.toString().c_str(),
                      WiFi.RSSI());
        _pendingActive = false;
        _pendingSsid = "";
        _pendingPassword = "";
        _pendingStartMs = 0;
      }
    } else if (st == WL_WRONG_PASSWORD) {
      // Wrong password is definitive and won't recover without user action.
      Serial.printf("[net] connect failed ssid=%s status=%s(%d)\n",
                    _pendingSsid.c_str(),
                    wifiStatusToString(st),
                    static_cast<int>(st));
      _pendingActive = false;
      _pendingSsid = "";
      _pendingPassword = "";
      _pendingStartMs = 0;
    } else if ((millis() - _pendingStartMs) > kPendingConnectTimeoutMs) {
      Serial.printf("[net] connect timeout ssid=%s\n", _pendingSsid.c_str());
      _pendingActive = false;
      _pendingSsid = "";
      _pendingPassword = "";
      _pendingStartMs = 0;
    }
  }

  // Keep internal state and the SDK in sync: some WiFi stacks can drop AP mode unexpectedly.
  if (_apMode) {
    const WiFiMode_t mode = WiFi.getMode();
    if (mode == WIFI_STA) {
      startAp();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    _lastStaOkMs = millis();
    if (_apMode) {
      const int clients = WiFi.softAPgetStationNum();
      if (clients > 0) {
        _apClientLastSeenMs = millis();
      } else if (_apClientLastSeenMs == 0) {
        _apClientLastSeenMs = millis();
      } else if ((millis() - _apClientLastSeenMs) > kStopApAfterNoClientsMs) {
        stopAp();
      }
      if (_apMode) _dns.processNextRequest();
    }
    return;
  }

  // Periodic reconnect attempts (helps after router reboot / power loss).
  const uint32_t nowMs = millis();
  if ((nowMs - _lastReconnectAttemptMs) > 30000UL) {
    _lastReconnectAttemptMs = nowMs;
    if (_savedCount > 0) {
      int idx = findSavedIndex(_lastSavedSsid);
      if (idx < 0) idx = 0;
      if (idx >= 0 && idx < _savedCount) {
        const String ssid = _saved[idx].ssid;
        const String password = _saved[idx].password;
        if (ssid.length()) {
          WiFi.mode(_apMode ? WIFI_AP_STA : WIFI_STA);
          WiFi.setAutoReconnect(true);
          WiFi.hostname(_hostName);
          if (_staDhcp) {
            WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
          } else if (!isZeroIp(_staIp) && !isZeroIp(_staGateway) && !isZeroIp(_staSubnet)) {
            WiFi.config(_staIp, _staGateway, _staSubnet, _staDns1, _staDns2);
          }
          WiFi.begin(ssid.c_str(), password.c_str());
          Serial.printf("[net] reconnect ssid=%s\n", ssid.c_str());
        } else {
          WiFi.reconnect();
        }
      }
    } else {
      WiFi.reconnect();
    }
  }

  if (!_apMode && (millis() - _lastStaOkMs) > 5000) {
    startAp();
  }

  if (_apMode) {
    _dns.processNextRequest();
  }
}

bool WifiController::isApMode() const { return _apMode; }

String WifiController::apSsid() const { return _apSsid; }

String WifiController::staSsid() const {
  if (WiFi.status() != WL_CONNECTED) return "";
  return WiFi.SSID();
}

String WifiController::ipString() const {
  if (_apMode) return WiFi.softAPIP().toString();
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return "";
}

String WifiController::hostName() const { return _hostName; }

bool WifiController::staDhcp() const { return _staDhcp; }

String WifiController::staStaticIpString() const {
  if (_staDhcp) return "";
  if (isZeroIp(_staIp)) return "";
  return _staIp.toString();
}

String WifiController::scanJson() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i += 1) {
    if (i) json += ",";
    json += "{\"ssid\":";
    json += "\"";
    json += WiFi.SSID(i);
    json += "\"";
    json += ",\"rssi\":";
    json += WiFi.RSSI(i);
    json += ",\"secure\":";
    json += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "false" : "true";
    json += "}";
  }
  json += "]";
  WiFi.scanDelete();
  return json;
}

bool WifiController::connectTo(const String &ssid, const String &password, uint32_t timeoutMs) {
  // Keep (or start) the AP during STA connect so the web UI doesn't get disconnected mid-request.
  // We intentionally start it even if we're currently STA-connected, so there's always a fallback.
  startAp();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(); // ensure clean state before switching networks (does not erase saved creds)
  delay(80);
  WiFi.hostname(_hostName);
  if (_staDhcp) {
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  } else if (!isZeroIp(_staIp) && !isZeroIp(_staGateway) && !isZeroIp(_staSubnet)) {
    WiFi.config(_staIp, _staGateway, _staSubnet, _staDns1, _staDns2);
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  _pendingActive = true;
  _pendingSsid = ssid;
  _pendingPassword = password;
  _pendingStartMs = millis();
  Serial.printf("[net] connecting ssid=%s (timeout=%lums)\n", ssid.c_str(), static_cast<unsigned long>(timeoutMs));

  const uint32_t start = millis();
  wl_status_t lastSt = WiFi.status();
  Serial.printf("[net] connect status=%s(%d)\n", wifiStatusToString(lastSt), static_cast<int>(lastSt));
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(150);
    yield();
    if (_apMode) _dns.processNextRequest();
    const wl_status_t st = WiFi.status();
    if (st != lastSt) {
      lastSt = st;
      Serial.printf("[net] connect status=%s(%d)\n", wifiStatusToString(st), static_cast<int>(st));
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[net] connect failed ssid=%s status=%s(%d)\n",
                  ssid.c_str(),
                  wifiStatusToString(WiFi.status()),
                  static_cast<int>(WiFi.status()));
    // Ensure AP is still up for retries (some stacks disable AP during failed STA attempts).
    startAp();
    // Keep pending active so if the connect completes shortly after timeout, it will be saved.
    return false;
  }

  _lastStaOkMs = millis();
  rememberOnSuccess(ssid, password);
  _pendingActive = false;
  _pendingSsid = "";
  _pendingPassword = "";
  _pendingStartMs = 0;
  Serial.printf("[net] connected ssid=%s ip=%s rssi=%d\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  // Don't stop AP here: this HTTP request likely came through the AP, and stopping it
  // would abort the response. `tick()` will stop it shortly after we return.
  return true;
}

bool WifiController::beginConnect(const String &ssid, const String &password) {
  if (!ssid.length()) return false;
  startAp();
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect();
  delay(80);
  WiFi.hostname(_hostName);
  if (_staDhcp) {
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  } else if (!isZeroIp(_staIp) && !isZeroIp(_staGateway) && !isZeroIp(_staSubnet)) {
    WiFi.config(_staIp, _staGateway, _staSubnet, _staDns1, _staDns2);
  }
  WiFi.begin(ssid.c_str(), password.c_str());

  _pendingActive = true;
  _pendingSsid = ssid;
  _pendingPassword = password;
  _pendingStartMs = millis();
  Serial.printf("[net] connect start ssid=%s\n", ssid.c_str());
  return true;
}

bool WifiController::connectInProgress() const { return _pendingActive; }

String WifiController::connectTargetSsid() const { return _pendingSsid; }

String WifiController::savedJson() const {
  DynamicJsonDocument doc(1024);
  doc["ok"] = true;
  doc["count"] = _savedCount;
  doc["last"] = _lastSavedSsid;
  JsonArray nets = doc.createNestedArray("nets");
  for (uint8_t i = 0; i < _savedCount; i += 1) {
    JsonObject n = nets.createNestedObject();
    n["ssid"] = _saved[i].ssid;
    n["last"] = (_saved[i].ssid == _lastSavedSsid);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool WifiController::forgetSaved(const String &ssid) {
  const int idx = findSavedIndex(ssid);
  if (idx < 0) return false;
  for (uint8_t i = static_cast<uint8_t>(idx) + 1; i < _savedCount; i += 1) {
    _saved[i - 1] = _saved[i];
  }
  _savedCount -= 1;
  if (_lastSavedSsid == ssid) _lastSavedSsid = "";
  saveSaved();
  return true;
}

uint8_t WifiController::savedCount() const { return _savedCount; }

void WifiController::resetAndReboot() {
  LittleFS.remove(kWifiStorePath);
  WiFi.disconnect(true);
  ESP.eraseConfig();
  delay(250);
  ESP.restart();
}

void WifiController::startAp() {
  const bool wasAp = _apMode;
  _apMode = true;

  WiFi.mode(WIFI_AP_STA);
  if (wasAp) _dns.stop();

  bool ok = false;
  if (_apPassword.length() >= 8) {
    ok = WiFi.softAP(_apSsid.c_str(), _apPassword.c_str());
    if (!ok) {
      // Fallback: open AP (better than no AP).
      ok = WiFi.softAP(_apSsid.c_str());
    }
  } else {
    ok = WiFi.softAP(_apSsid.c_str());
  }
  if (!ok) {
    Serial.printf("[net] ap start failed ssid=%s\n", _apSsid.c_str());
  }
  delay(100);

  _dns.start(53, "*", WiFi.softAPIP());
  _apClientLastSeenMs = millis();

  if (!wasAp) {
    Serial.printf("[net] ap started ssid=%s ip=%s%s\n",
                  _apSsid.c_str(),
                  WiFi.softAPIP().toString().c_str(),
                  ok ? "" : " (FAILED)");
  }
}

void WifiController::stopAp() {
  if (!_apMode) return;
  _dns.stop();
  WiFi.softAPdisconnect(true);
  _apMode = false;
  _apClientLastSeenMs = 0;
  Serial.println(F("[net] ap stopped"));
}
