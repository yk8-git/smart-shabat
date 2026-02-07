#include "WebUi.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

#include "AppConfig.h"
#include "EmbeddedUi.h"
#include "OverrideWindows.h"

namespace {
String jsonError(const String &msg) {
  DynamicJsonDocument doc(256);
  doc["ok"] = false;
  doc["error"] = msg;
  String out;
  serializeJson(doc, out);
  return out;
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

} // namespace

WebUi::WebUi(uint16_t port) : _server(port) {}

void WebUi::begin(AppConfig &cfg,
                  WifiController &wifi,
                  TimeKeeper &time,
                  RelayController &relay,
                  ZmanimDb &zmanim,
                  HolidayDb &holidays,
                  ScheduleEngine &schedule,
                  OtaUpdater &ota,
                  StatusIndicator &indicator,
                  HistoryLog &history) {
  _cfg = &cfg;
  _wifi = &wifi;
  _time = &time;
  _relay = &relay;
  _zmanim = &zmanim;
  _holidays = &holidays;
  _schedule = &schedule;
  _ota = &ota;
  _indicator = &indicator;
  _history = &history;

  setupRoutes();
  _server.begin();
}

void WebUi::tick() { _server.handleClient(); }

void WebUi::sendJson(int code, const String &json) {
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(code, "application/json; charset=utf-8", json);
}

void WebUi::setupRoutes() {
  _server.on("/status.txt", HTTP_GET, [this]() {
    String status = "OK";
    if (!_time->isTimeValid()) {
      status = "TIME_INVALID";
    } else if (_zmanim && !_zmanim->hasData()) {
      status = "MISSING_ZMANIM";
    } else if (_wifi->isApMode()) {
      status = "AP_MODE";
    } else if (_holidays && !_holidays->hasData()) {
      status = "MISSING_HOLIDAYS";
    }

    if (status == "OK" && _cfg->ntpEnabled) {
      if (WiFi.status() == WL_CONNECTED && _time->lastNtpSyncUtc() == 0) {
        status = "WAITING_NTP";
      }
    }

    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "text/plain; charset=utf-8", status + "\n");
  });

  _server.on("/api/status", HTTP_GET, [this]() {
    DynamicJsonDocument doc(2560);
    doc["ok"] = true;
    doc["version"] = SHABAT_RELAY_VERSION;
    const bool lite = _server.hasArg("lite") && _server.arg("lite") == "1";

    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["mac"] = WiFi.macAddress();
    wifi["apMac"] = WiFi.softAPmacAddress();
    wifi["apMode"] = _wifi->isApMode();
    wifi["apSsid"] = _wifi->apSsid();
    wifi["apIp"] = _wifi->isApMode() ? WiFi.softAPIP().toString() : "";
    wifi["apClients"] = _wifi->isApMode() ? WiFi.softAPgetStationNum() : 0;
    wifi["staSsid"] = _wifi->staSsid();
    wifi["staIp"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
    wifi["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    wifi["staStatus"] = wifiStatusToString(WiFi.status());
    wifi["staStatusCode"] = static_cast<int>(WiFi.status());
    wifi["ip"] = _wifi->ipString();
    wifi["hostName"] = _wifi->hostName();
    wifi["staDhcp"] = _wifi->staDhcp();
    wifi["staStaticIp"] = _wifi->staStaticIpString();

    if (!lite) {
      JsonObject time = doc.createNestedObject("time");
      time["valid"] = _time->isTimeValid();
      time["utc"] = static_cast<uint32_t>(_time->nowUtc());
      time["local"] = static_cast<uint32_t>(_time->nowLocal(*_cfg));
      time["tzOffsetSeconds"] = _time->localOffsetSeconds(*_cfg);
      time["source"] = _time->timeSource();
      time["lastNtpSyncUtc"] = static_cast<uint32_t>(_time->lastNtpSyncUtc());
      time["lastManualSetUtc"] = static_cast<uint32_t>(_time->lastManualSetUtc());
      time["ntpResyncMinutes"] = _cfg->ntpResyncMinutes;
      time["ntpServer"] = _cfg->ntpServer;
      time["tzOffsetMinutes"] = _cfg->tzOffsetMinutes;
      time["dstMode"] = _cfg->dstMode;
      time["dstActive"] = _time->dstActive(*_cfg);
      time["nextDstChangeLocal"] = static_cast<uint32_t>(_time->nextDstChangeLocal(*_cfg));
    }

    JsonObject relay = doc.createNestedObject("relay");
    relay["on"] = _relay->isOn();
    relay["gpio"] = _cfg->relayGpio;
    relay["activeLow"] = _cfg->relayActiveLow;

    JsonObject op = doc.createNestedObject("operation");
    op["runMode"] = _cfg->runMode;
    const uint32_t nowUtc = static_cast<uint32_t>(_time->nowUtc());
    const ActiveWindowOverride ov = overridesFindActive(*_cfg, nowUtc);
    op["overrideActive"] = ov.active;
    op["overrideStateOn"] = ov.stateOn;
    op["overrideEndUtc"] = ov.active ? ov.endUtc : 0;

    if (_schedule) {
      const ScheduleStatus st = _schedule->status();
      JsonObject sched = doc.createNestedObject("schedule");
      sched["ok"] = st.ok;
      sched["inHolyTime"] = st.inHolyTime;
      sched["hasZmanim"] = st.hasZmanim;
      sched["hasHolidays"] = st.hasHolidays;
      sched["nextChangeLocal"] = st.nextChangeLocal;
      sched["nextStateOn"] = st.nextStateOn;
      sched["errorCode"] = st.errorCode;
      sched["error"] = st.error;
    }

    String out;
    serializeJson(doc, out);
    sendJson(200, out);
  });

  _server.on("/api/time", HTTP_GET, [this]() {
    DynamicJsonDocument doc(768);
    doc["ok"] = true;
    doc["valid"] = _time->isTimeValid();
    doc["utc"] = static_cast<uint32_t>(_time->nowUtc());
    doc["local"] = static_cast<uint32_t>(_time->nowLocal(*_cfg));
    doc["tzOffsetSeconds"] = _time->localOffsetSeconds(*_cfg);
    doc["source"] = _time->timeSource();
    doc["lastNtpSyncUtc"] = static_cast<uint32_t>(_time->lastNtpSyncUtc());
    doc["lastManualSetUtc"] = static_cast<uint32_t>(_time->lastManualSetUtc());
    doc["ntpResyncMinutes"] = _cfg->ntpResyncMinutes;
    doc["ntpServer"] = _cfg->ntpServer;
    doc["tzOffsetMinutes"] = _cfg->tzOffsetMinutes;
    doc["dstMode"] = _cfg->dstMode;
    doc["dstActive"] = _time->dstActive(*_cfg);
    doc["nextDstChangeLocal"] = static_cast<uint32_t>(_time->nextDstChangeLocal(*_cfg));
    String out;
    serializeJson(doc, out);
    sendJson(200, out);
  });

  _server.on("/api/history", HTTP_GET, [this]() {
    uint16_t limit = 40;
    if (_server.hasArg("limit")) {
      const int v = _server.arg("limit").toInt();
      if (v > 0 && v <= 200) limit = static_cast<uint16_t>(v);
    }
    if (!_history) {
      sendJson(200, "{\"ok\":true,\"items\":[]}");
      return;
    }
    sendJson(200, _history->toJson(limit));
  });

  _server.on("/api/history/clear", HTTP_POST, [this]() {
    if (_history) _history->clear();
    sendJson(200, "{\"ok\":true}");
  });

  _server.on("/api/schedule", HTTP_GET, [this]() {
    if (!_schedule) {
      sendJson(500, jsonError("schedule not initialized"));
      return;
    }
    const ScheduleStatus st = _schedule->status();
    DynamicJsonDocument doc(4096);
    doc["ok"] = true;
    JsonObject s = doc.createNestedObject("status");
    s["ok"] = st.ok;
    s["inHolyTime"] = st.inHolyTime;
    s["hasZmanim"] = st.hasZmanim;
    s["hasHolidays"] = st.hasHolidays;
    s["nowLocal"] = st.nowLocal;
    s["nextChangeLocal"] = st.nextChangeLocal;
    s["nextStateOn"] = st.nextStateOn;
    s["errorCode"] = st.errorCode;
    s["error"] = st.error;
    doc["upcoming"] = serialized(_schedule->upcomingJson(10));
    String out;
    serializeJson(doc, out);
    sendJson(200, out);
  });

  _server.on("/api/config", HTTP_GET, [this]() { sendJson(200, appcfg::toJson(*_cfg)); });

	  _server.on("/api/config", HTTP_POST, [this]() {
	    if (!_server.hasArg("plain")) {
	      sendJson(400, jsonError("missing body"));
	      return;
	    }
	
	    const AppConfig prev = *_cfg;
	    AppConfig next = prev;
	    if (!appcfg::fromJson(next, _server.arg("plain"))) {
	      sendJson(400, jsonError("invalid json"));
	      return;
	    }
	
	    auto sameIp = [](const IPAddress &a, const IPAddress &b) -> bool {
	      for (uint8_t i = 0; i < 4; i += 1) {
	        if (a[i] != b[i]) return false;
	      }
	      return true;
	    };
	    const bool networkChanged =
	      (prev.hostName != next.hostName) || (prev.apSsid != next.apSsid) || (prev.apPassword != next.apPassword) ||
	      (prev.staDhcp != next.staDhcp) || !sameIp(prev.staIp, next.staIp) || !sameIp(prev.staGateway, next.staGateway) ||
	      !sameIp(prev.staSubnet, next.staSubnet) || !sameIp(prev.staDns1, next.staDns1) || !sameIp(prev.staDns2, next.staDns2);

	    *_cfg = next;
	    appcfg::save(*_cfg);
	    _relay->applyConfig(*_cfg);
	    if (_indicator) _indicator->applyConfig(*_cfg);
	    if (_schedule) _schedule->invalidate();

    if (_history) {
	      const uint32_t t = _time && _time->isTimeValid() ? static_cast<uint32_t>(_time->nowLocal(*_cfg)) : 0;
	      _history->add(t, HistoryKind::Boot, "ההגדרות נשמרו");
	    }
	    sendJson(200, networkChanged ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":true}");
	    if (networkChanged) {
	      delay(500);
	      ESP.restart();
	    }
	  });

  _server.on("/api/time", HTTP_POST, [this]() {
    if (!_server.hasArg("plain")) {
      sendJson(400, jsonError("missing body"));
      return;
    }
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
      sendJson(400, jsonError("invalid json"));
      return;
    }
    if (!doc.containsKey("utc")) {
      sendJson(400, jsonError("missing utc"));
      return;
    }
    time_t utc = doc["utc"].as<uint32_t>();
    _time->setManualUtc(utc);
    sendJson(200, "{\"ok\":true}");
  });

  _server.on("/api/ntp/sync", HTTP_POST, [this]() {
    const bool ok = _time->syncNtpNow(*_cfg);
    sendJson(ok ? 200 : 503, ok ? "{\"ok\":true}" : jsonError("ntp failed"));
  });

  _server.on("/api/ota/status", HTTP_GET, [this]() {
    if (!_ota) {
      sendJson(500, jsonError("ota not initialized"));
      return;
    }
    sendJson(200, _ota->statusJson(*_cfg, *_time, *_schedule));
  });

  _server.on("/api/ota/check", HTTP_POST, [this]() {
    if (!_ota) {
      sendJson(500, jsonError("ota not initialized"));
      return;
    }
    const OtaCheckResult r = _ota->checkNow(*_cfg);
    DynamicJsonDocument doc(768);
    doc["ok"] = r.ok;
    doc["available"] = r.available;
    doc["availableVersion"] = r.availableVersion;
    doc["message"] = r.message;
    String out;
    serializeJson(doc, out);
    if (_history) {
      const uint32_t t = _time && _time->isTimeValid() ? static_cast<uint32_t>(_time->nowLocal(*_cfg)) : 0;
      if (r.ok) {
        const String msg = r.available ? ("נמצא עדכון: " + r.availableVersion) : "בדיקת עדכונים: אין עדכון";
        _history->add(t, HistoryKind::Update, msg);
      } else {
        _history->add(t, HistoryKind::Update, "בדיקת עדכונים נכשלה");
      }
    }
    sendJson(r.ok ? 200 : 503, out);
  });

  // Dev helper: set manifest URL to the requester's IP (useful when connected to the device Hotspot).
  // Example: run `python3 -m http.server 8000` on your laptop, then call:
  // POST /api/ota/manifest_from_client {"port":8000,"path":"/ota.json"}
  _server.on("/api/ota/manifest_from_client", HTTP_POST, [this]() {
    uint16_t port = 8000;
    String path = "/ota.json";

    if (_server.hasArg("plain") && _server.arg("plain").length()) {
      DynamicJsonDocument doc(384);
      DeserializationError err = deserializeJson(doc, _server.arg("plain"));
      if (!err) {
        const int p = doc["port"] | 0;
        const String pa = doc["path"] | "";
        if (p > 0 && p <= 65535) port = static_cast<uint16_t>(p);
        if (pa.length() && pa.startsWith("/")) path = pa;
      }
    }

    const IPAddress ip = _server.client().remoteIP();
    const String url = "http://" + ip.toString() + ":" + String(port) + path;
    _cfg->otaManifestUrl = url;
    appcfg::save(*_cfg);

    if (_history) {
      const uint32_t t = _time && _time->isTimeValid() ? static_cast<uint32_t>(_time->nowLocal(*_cfg)) : 0;
      _history->add(t, HistoryKind::Update, "עודכן קישור עדכון (מקומי)");
    }

    DynamicJsonDocument outDoc(384);
    outDoc["ok"] = true;
    outDoc["manifestUrl"] = url;
    String out;
    serializeJson(outDoc, out);
    sendJson(200, out);
  });

  _server.on("/api/ota/update", HTTP_POST, [this]() {
    if (!_ota) {
      sendJson(500, jsonError("ota not initialized"));
      return;
    }

    if (_schedule) {
      const ScheduleStatus st = _schedule->status();
      if (st.ok && st.inHolyTime) {
        sendJson(403, jsonError("blocked by holy time"));
        return;
      }
    }

    sendJson(200, "{\"ok\":true,\"started\":true}");
    delay(200);
    if (_history) {
      const uint32_t t = _time && _time->isTimeValid() ? static_cast<uint32_t>(_time->nowLocal(*_cfg)) : 0;
      _history->add(t, HistoryKind::Update, "מתחיל עדכון תוכנה");
    }
    _ota->updateNow(*_cfg);
  });

  _server.on("/api/wifi/status", HTTP_GET, [this]() {
    DynamicJsonDocument doc(512);
    doc["ok"] = true;
    doc["apMode"] = _wifi->isApMode();
    doc["apSsid"] = _wifi->apSsid();
    doc["apIp"] = _wifi->isApMode() ? WiFi.softAPIP().toString() : "";
    doc["apClients"] = _wifi->isApMode() ? WiFi.softAPgetStationNum() : 0;
    doc["staSsid"] = _wifi->staSsid();
    doc["staIp"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
    doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    doc["staStatus"] = wifiStatusToString(WiFi.status());
    doc["staStatusCode"] = static_cast<int>(WiFi.status());
    doc["connecting"] = _wifi->connectInProgress();
    doc["targetSsid"] = _wifi->connectTargetSsid();
    doc["ip"] = _wifi->ipString();
    String out;
    serializeJson(doc, out);
    sendJson(200, out);
  });

  _server.on("/api/wifi/scan", HTTP_GET, [this]() { sendJson(200, _wifi->scanJson()); });

  _server.on("/api/wifi/saved", HTTP_GET, [this]() { sendJson(200, _wifi->savedJson()); });

  _server.on("/api/wifi/forget", HTTP_POST, [this]() {
    if (!_server.hasArg("plain")) {
      sendJson(400, jsonError("missing body"));
      return;
    }
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
      sendJson(400, jsonError("invalid json"));
      return;
    }
    const String ssid = doc["ssid"].as<String>();
    if (!ssid.length()) {
      sendJson(400, jsonError("missing ssid"));
      return;
    }
    const bool ok = _wifi->forgetSaved(ssid);
    sendJson(ok ? 200 : 404, ok ? "{\"ok\":true}" : jsonError("not found"));
  });

  _server.on("/api/wifi/connect", HTTP_POST, [this]() {
    if (!_server.hasArg("plain")) {
      sendJson(400, jsonError("missing body"));
      return;
    }
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
      sendJson(400, jsonError("invalid json"));
      return;
    }
    const String ssid = doc["ssid"].as<String>();
    const String password = doc["password"].as<String>();
    if (!ssid.length()) {
      sendJson(400, jsonError("missing ssid"));
      return;
    }

    // Start connection and report immediate status; the connection may complete asynchronously.
    // This makes the UX robust even when AP+STA channel switching causes delays.
    if (!_wifi->beginConnect(ssid, password)) {
      sendJson(500, jsonError("beginConnect failed"));
      return;
    }

    // Brief wait to catch quick success/fail (wrong password, etc.)
    const uint32_t start = millis();
    wl_status_t st = WiFi.status();
    while ((millis() - start) < 5000) {
      delay(120);
      yield();
      st = WiFi.status();
      if (st == WL_CONNECTED || st == WL_WRONG_PASSWORD) break;
    }

    // Allow the controller to persist credentials if we already connected while handling this request.
    _wifi->tick();

    const bool connected = (WiFi.status() == WL_CONNECTED);
    IPAddress ip = connected ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
    if (connected && (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0)) {
      // Give DHCP a moment; improves UX (redirect) for some routers.
      const uint32_t s2 = millis();
      while ((millis() - s2) < 3000) {
        delay(120);
        yield();
        ip = WiFi.localIP();
        if (!(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0)) break;
      }
    }
    DynamicJsonDocument outDoc(768);
    outDoc["ok"] = true;
    outDoc["started"] = true;
    outDoc["connected"] = connected;
    outDoc["status"] = static_cast<int>(WiFi.status());
    outDoc["statusText"] = wifiStatusToString(WiFi.status());
    outDoc["connecting"] = _wifi->connectInProgress();
    outDoc["targetSsid"] = _wifi->connectTargetSsid();
    if (connected) {
      outDoc["ssid"] = WiFi.SSID();
      outDoc["ip"] = ip.toString();
      outDoc["rssi"] = WiFi.RSSI();
    }
    String out;
    serializeJson(outDoc, out);
    sendJson(200, out);
  });

  _server.on("/api/wifi/reset", HTTP_POST, [this]() {
    if (_history) {
      const uint32_t t = _time && _time->isTimeValid() ? static_cast<uint32_t>(_time->nowLocal(*_cfg)) : 0;
      _history->add(t, HistoryKind::Network, "איפוס Wi‑Fi");
    }
    sendJson(200, "{\"ok\":true}");
    delay(200);
    _wifi->resetAndReboot();
  });

  _server.on("/api/factory_reset", HTTP_POST, [this]() {
    sendJson(200, "{\"ok\":true,\"reboot\":true}");
    delay(250);

    Serial.println(F("[reset] factory reset"));

    // Wipe user data in LittleFS (config, wifi list, history, OTA state, relay state, etc.)
    LittleFS.format();

    // Also wipe SDK Wi‑Fi credentials.
    WiFi.disconnect(true);
    ESP.eraseConfig();
    delay(250);
    ESP.restart();
  });

  // Static UI (ESP8266WebServer::serveStatic returns void in this core)
  _server.on("/", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "no-store");
    _server.send_P(200, "text/html; charset=utf-8", kEmbeddedIndexHtml);
  });

  _server.on("/styles.css", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "no-store");
    _server.send_P(200, "text/css; charset=utf-8", kEmbeddedStylesCss);
  });

  _server.on("/app.js", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "no-store");
    _server.send_P(200, "application/javascript; charset=utf-8", kEmbeddedAppJs);
  });

  _server.on("/favicon.ico", HTTP_GET, [this]() { _server.send(204); });

  _server.onNotFound([this]() {
    if (_server.uri().startsWith("/api/")) {
      sendJson(404, jsonError("not found"));
      return;
    }
    _server.sendHeader("Location", "/", true);
    _server.send(302, "text/plain", "");
  });
}
