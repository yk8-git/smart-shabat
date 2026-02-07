#pragma once

#include <Arduino.h>
#include <DNSServer.h>

#include "AppConfig.h"

class WifiController {
public:
  void begin(const AppConfig &cfg);
  void tick();

  bool isApMode() const;
  String apSsid() const;
  String staSsid() const;
  String ipString() const;
  String hostName() const;
  bool staDhcp() const;
  String staStaticIpString() const;

  String scanJson();
  bool connectTo(const String &ssid, const String &password, uint32_t timeoutMs);
  String savedJson() const;
  bool forgetSaved(const String &ssid);
  uint8_t savedCount() const;
  void resetAndReboot();

private:
  void applyNetworkConfig(const AppConfig &cfg);
  void startAp();
  void stopAp();
  void loadSaved();
  bool saveSaved() const;
  void rememberOnSuccess(const String &ssid, const String &password);
  int findSavedIndex(const String &ssid) const;

  bool _apMode = false;
  DNSServer _dns;
  String _apSsid;
  String _hostName;
  bool _staDhcp = true;
  IPAddress _staIp = IPAddress(0, 0, 0, 0);
  IPAddress _staGateway = IPAddress(0, 0, 0, 0);
  IPAddress _staSubnet = IPAddress(0, 0, 0, 0);
  IPAddress _staDns1 = IPAddress(0, 0, 0, 0);
  IPAddress _staDns2 = IPAddress(0, 0, 0, 0);

  String _apPassword;

  uint32_t _lastStaOkMs = 0;
  uint32_t _apClientLastSeenMs = 0;
  uint32_t _lastReconnectAttemptMs = 0;

  struct SavedNetwork {
    String ssid;
    String password;
  };
  static constexpr uint8_t kMaxSavedNetworks = 5;
  SavedNetwork _saved[kMaxSavedNetworks];
  uint8_t _savedCount = 0;
  String _lastSavedSsid;
};
