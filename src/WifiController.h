#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>

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
  // Non-blocking connect (keeps AP active during attempt). Results are visible in /api/wifi/status.
  bool beginConnect(const String &ssid, const String &password);
  // Non-blocking connect request designed for the Hotspot (AP) UI:
  // it defers the actual WiFi.begin() to allow the HTTP response to flush, then
  // scans for the target SSID and restarts the AP on the target channel (AP+STA
  // can only operate on a single channel), and only then starts the STA connect.
  // If `channelHint` / `bssidHint` are provided (e.g. from a recent scan), scanning is skipped.
  // If `simpleStaOnly` is true, the controller will temporarily stop the AP and attempt a plain STA connection.
  // If it fails, the AP is restored so the user isn't locked out.
  bool requestConnect(const String &ssid,
                      const String &password,
                      int32_t channelHint = 0,
                      const uint8_t *bssidHint = nullptr,
                      bool simpleStaOnly = false);
  bool connectInProgress() const;
  String connectTargetSsid() const;
  uint8_t connectStageCode() const;
  int32_t connectTargetChannel() const;
  int32_t apChannel() const;
  int32_t lastConnectFailCode() const;
  uint16_t lastStaDisconnectReason() const;
  uint16_t lastStaDisconnectReasonRaw() const;
  bool lastStaDisconnectWasExpected() const;
  int32_t sdkStationStatusCode() const;
  bool connectSimpleStaOnly() const;
  String logJson() const;
  String savedJson() const;
  bool forgetSaved(const String &ssid);
  bool saveNetwork(const String &ssid, const String &password, bool makeLast = true);
  uint8_t savedCount() const;
  void resetAndReboot();

private:
  void applyNetworkConfig(const AppConfig &cfg);
  void startAp();
  void startApOnChannel(int32_t channel);
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

  // Pending connect attempt (for robust UX: connect may complete after the HTTP request returns)
  bool _pendingActive = false;
  String _pendingSsid;
  String _pendingPassword;
  uint32_t _pendingStartMs = 0;

  enum class PendingStage : uint8_t {
    None = 0,
    Deferred,  // wait a short time so the HTTP response can flush before AP changes
    Scanning,  // scan for target SSID to learn its channel
    SwitchingApChannel,
    StartingSta,
    Connecting,
  };
  PendingStage _pendingStage = PendingStage::None;
  uint32_t _pendingDeferUntilMs = 0;
  uint32_t _pendingScanStartMs = 0;
  int32_t _pendingTargetChannel = 0;
  uint8_t _pendingTargetBssid[6] = {0, 0, 0, 0, 0, 0};
  bool _pendingHasBssid = false;
  int32_t _apChannel = 0;
  bool _pendingSimpleStaOnly = false;

  int32_t _lastConnectFailCode = 0;
  // Last station disconnect reason we saw from the SDK (raw).
  uint16_t _lastStaDiscReason = 0;
  // Last "real" disconnect reason (ignores disconnects we intentionally trigger).
  uint16_t _lastStaDiscReasonReal = 0;
  uint32_t _lastStaDiscReasonMs = 0;
  WiFiEventHandler _staDiscHandler;
  uint8_t _staDiscExpectedCount = 0;
  bool _lastStaDiscExpected = false;
  int32_t _sdkStaStatus = 0;
  int32_t _sdkStaStatusLastLogged = -1;
  uint32_t _sdkStaStatusLastPollMs = 0;

  uint32_t _connectAttemptId = 0;
  uint32_t _connectLastBeginMs = 0;
  uint8_t _connectVariant = 0; // 0=chan+bssid, 1=chan-only, 2=no-hints

  struct WifiLogEntry {
    uint32_t ms = 0;
    uint32_t attemptId = 0;
    uint8_t stage = 0;
    uint8_t variant = 0;
    int8_t staStatus = 0;
    int8_t lastFail = 0;
    int8_t apCh = 0;
    int8_t targetCh = 0;
    uint16_t discReason = 0;
    uint8_t discExpected = 0;
    int8_t sdkSta = 0;
  };
  static constexpr uint8_t kWifiLogSize = 20;
  WifiLogEntry _wifiLog[kWifiLogSize];
  uint8_t _wifiLogHead = 0;

  void logWifiEvent();
  void beginPendingStaConnect();
  void expectStaDisconnect(uint8_t count = 1);
};
