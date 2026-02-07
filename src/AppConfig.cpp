#include "AppConfig.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
constexpr const char *kConfigPath = "/config.json";

String readFile(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) return "";
  String out;
  out.reserve(file.size());
  while (file.available()) {
    out += static_cast<char>(file.read());
  }
  return out;
}

bool writeFile(const char *path, const String &contents) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  size_t written = file.print(contents);
  return written == contents.length();
}
} // namespace

namespace appcfg {

String toJson(const AppConfig &cfg) {
  DynamicJsonDocument doc(4096);

  doc["deviceName"] = cfg.deviceName;

  JsonObject net = doc.createNestedObject("network");
  net["hostName"] = cfg.hostName;
  JsonObject sta = net.createNestedObject("sta");
  sta["dhcp"] = cfg.staDhcp;
  JsonObject st = sta.createNestedObject("static");
  st["ip"] = cfg.staIp.toString();
  st["gateway"] = cfg.staGateway.toString();
  st["subnet"] = cfg.staSubnet.toString();
  st["dns1"] = cfg.staDns1.toString();
  st["dns2"] = cfg.staDns2.toString();

  JsonObject ap = net.createNestedObject("ap");
  ap["ssid"] = cfg.apSsid;
  ap["passwordSet"] = cfg.apPassword.length() >= 8;

  JsonObject time = doc.createNestedObject("time");
  time["ntpEnabled"] = cfg.ntpEnabled;
  time["ntpServer"] = cfg.ntpServer;
  time["ntpResyncMinutes"] = cfg.ntpResyncMinutes;
  time["tzOffsetMinutes"] = cfg.tzOffsetMinutes;
  time["dstMode"] = cfg.dstMode;
  time["dstEnabled"] = cfg.dstEnabled;
  time["dstOffsetMinutes"] = cfg.dstOffsetMinutes;

  JsonObject loc = doc.createNestedObject("location");
  loc["name"] = cfg.locationName;
  loc["israel"] = cfg.israel;

  JsonObject halacha = doc.createNestedObject("halacha");
  halacha["minutesBeforeShkia"] = cfg.minutesBeforeShkia;
  halacha["minutesAfterTzeit"] = cfg.minutesAfterTzeit;

  JsonObject relay = doc.createNestedObject("relay");
  relay["gpio"] = cfg.relayGpio;
  relay["activeLow"] = cfg.relayActiveLow;
  relay["holyOnNo"] = cfg.relayHolyOnNo;
  relay["manualOverride"] = cfg.manualOverride;
  relay["manualRelayOn"] = cfg.manualRelayOn;

  JsonObject op = doc.createNestedObject("operation");
  op["runMode"] = cfg.runMode;
  JsonArray wins = op.createNestedArray("windows");
  for (uint8_t i = 0; i < cfg.windowCount && i < AppConfig::kMaxWindows; i += 1) {
    const ManualTimeWindow &w = cfg.windows[i];
    JsonObject o = wins.createNestedObject();
    o["startUtc"] = w.startUtc;
    o["endUtc"] = w.endUtc;
    o["on"] = w.on;
  }

  JsonObject led = doc.createNestedObject("led");
  led["gpio"] = cfg.statusLedGpio;
  led["activeLow"] = cfg.statusLedActiveLow;

  JsonObject ota = doc.createNestedObject("ota");
  ota["manifestUrl"] = cfg.otaManifestUrl;
  ota["auto"] = cfg.otaAuto;
  ota["checkHours"] = cfg.otaCheckHours;

  String out;
  serializeJson(doc, out);
  return out;
}

bool fromJson(AppConfig &cfg, const String &json) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  if (doc.containsKey("deviceName")) cfg.deviceName = doc["deviceName"].as<String>();

  JsonObject net = doc["network"];
  if (!net.isNull()) {
    if (net.containsKey("hostName")) cfg.hostName = net["hostName"].as<String>();

    JsonObject sta = net["sta"];
    if (!sta.isNull()) {
      if (sta.containsKey("dhcp")) cfg.staDhcp = sta["dhcp"].as<bool>();
      JsonObject st = sta["static"];
      if (!st.isNull()) {
        if (st.containsKey("ip")) cfg.staIp.fromString(st["ip"].as<String>());
        if (st.containsKey("gateway")) cfg.staGateway.fromString(st["gateway"].as<String>());
        if (st.containsKey("subnet")) cfg.staSubnet.fromString(st["subnet"].as<String>());
        if (st.containsKey("dns1")) cfg.staDns1.fromString(st["dns1"].as<String>());
        if (st.containsKey("dns2")) cfg.staDns2.fromString(st["dns2"].as<String>());
      }
    }

    JsonObject ap = net["ap"];
    if (!ap.isNull()) {
      if (ap.containsKey("ssid")) cfg.apSsid = ap["ssid"].as<String>();
      if (ap.containsKey("password")) {
        const String p = ap["password"].as<String>();
        cfg.apPassword = (p.length() >= 8) ? p : "";
      }
    }
  }

  JsonObject time = doc["time"];
  if (!time.isNull()) {
    if (time.containsKey("ntpEnabled")) cfg.ntpEnabled = time["ntpEnabled"].as<bool>();
    if (time.containsKey("ntpServer")) cfg.ntpServer = time["ntpServer"].as<String>();
    if (time.containsKey("ntpResyncMinutes")) cfg.ntpResyncMinutes = time["ntpResyncMinutes"].as<uint16_t>();
    if (time.containsKey("tzOffsetMinutes")) cfg.tzOffsetMinutes = time["tzOffsetMinutes"].as<int>();
    bool sawDstEnabled = false;
    if (time.containsKey("dstEnabled")) {
      cfg.dstEnabled = time["dstEnabled"].as<bool>();
      sawDstEnabled = true;
    }
    if (time.containsKey("dstMode")) {
      cfg.dstMode = time["dstMode"].as<uint8_t>();
    } else if (sawDstEnabled) {
      // Backwards compatibility: old configs used dstEnabled only (manual).
      cfg.dstMode = 2;
    }
    if (time.containsKey("dstOffsetMinutes")) cfg.dstOffsetMinutes = time["dstOffsetMinutes"].as<int>();
  }

  JsonObject loc = doc["location"];
  if (!loc.isNull()) {
    if (loc.containsKey("name")) cfg.locationName = loc["name"].as<String>();
    if (loc.containsKey("israel")) cfg.israel = loc["israel"].as<bool>();
  }

  JsonObject halacha = doc["halacha"];
  if (!halacha.isNull()) {
    if (halacha.containsKey("minutesBeforeShkia")) cfg.minutesBeforeShkia = halacha["minutesBeforeShkia"].as<int>();
    if (halacha.containsKey("minutesAfterTzeit")) cfg.minutesAfterTzeit = halacha["minutesAfterTzeit"].as<int>();
  }

  JsonObject relay = doc["relay"];
  if (!relay.isNull()) {
    if (relay.containsKey("gpio")) cfg.relayGpio = relay["gpio"].as<int>();
    if (relay.containsKey("activeLow")) cfg.relayActiveLow = relay["activeLow"].as<bool>();
    if (relay.containsKey("holyOnNo")) cfg.relayHolyOnNo = relay["holyOnNo"].as<bool>();
    if (relay.containsKey("manualOverride")) cfg.manualOverride = relay["manualOverride"].as<bool>();
    if (relay.containsKey("manualRelayOn")) cfg.manualRelayOn = relay["manualRelayOn"].as<bool>();
  }

  JsonObject op = doc["operation"];
  if (!op.isNull()) {
    if (op.containsKey("runMode")) cfg.runMode = op["runMode"].as<uint8_t>();
    if (op.containsKey("windows")) {
      cfg.windowCount = 0;
      JsonArray wins = op["windows"].as<JsonArray>();
      if (!wins.isNull()) {
        for (JsonObject w : wins) {
          if (cfg.windowCount >= AppConfig::kMaxWindows) break;
          const uint32_t startUtc = w["startUtc"] | 0;
          const uint32_t endUtc = w["endUtc"] | 0;
          const bool on = w["on"] | false;
          if (startUtc == 0 || endUtc == 0) continue;
          if (endUtc <= startUtc) continue;
          cfg.windows[cfg.windowCount].startUtc = startUtc;
          cfg.windows[cfg.windowCount].endUtc = endUtc;
          cfg.windows[cfg.windowCount].on = on;
          cfg.windowCount += 1;
        }
      }
    }
  }

  JsonObject led = doc["led"];
  if (!led.isNull()) {
    if (led.containsKey("gpio")) cfg.statusLedGpio = led["gpio"].as<int>();
    if (led.containsKey("activeLow")) cfg.statusLedActiveLow = led["activeLow"].as<bool>();
  }

  JsonObject ota = doc["ota"];
  if (!ota.isNull()) {
    if (ota.containsKey("manifestUrl")) cfg.otaManifestUrl = ota["manifestUrl"].as<String>();
    if (ota.containsKey("auto")) cfg.otaAuto = ota["auto"].as<bool>();
    if (ota.containsKey("checkHours")) cfg.otaCheckHours = ota["checkHours"].as<uint16_t>();
  }

  return true;
}

bool load(AppConfig &cfg) {
  if (!LittleFS.exists(kConfigPath)) return false;
  const String raw = readFile(kConfigPath);
  if (!raw.length()) return false;
  return fromJson(cfg, raw);
}

bool save(const AppConfig &cfg) {
  return writeFile(kConfigPath, toJson(cfg));
}

} // namespace appcfg
