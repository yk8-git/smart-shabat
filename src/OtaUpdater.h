#pragma once

#include <Arduino.h>

#include "AppConfig.h"
#include "ScheduleEngine.h"
#include "TimeKeeper.h"

struct OtaCheckResult {
  bool ok = false;
  bool available = false;
  String availableVersion;
  String message;
};

class OtaUpdater {
public:
  void begin();
  void tick(const AppConfig &cfg, const TimeKeeper &time, const ScheduleEngine &schedule);

  String statusJson(const AppConfig &cfg, const TimeKeeper &time, const ScheduleEngine &schedule) const;
  OtaCheckResult checkNow(const AppConfig &cfg);
  bool updateNow(const AppConfig &cfg);

  bool hasUpdateAvailable() const;
  String lastError() const;
  void clearAvailableState();

private:
  static bool isSafeForAutoUpdate(const ScheduleStatus &st);
  static bool isBlockedByHolyTime(const ScheduleStatus &st);

  static bool isHttpsUrl(const String &url);
  static int compareVersions(const String &a, const String &b);

  bool fetchManifest(const AppConfig &cfg, String &outVersion, String &outBinUrl, String &outMd5, String &outNotes);
  void loadState();
  void saveState() const;

  String _availableVersion;
  String _availableBinUrl;
  String _availableMd5;
  String _availableNotes;
  String _lastError;

  uint32_t _lastCheckUtc = 0;
  uint32_t _lastAttemptUtc = 0;
};
