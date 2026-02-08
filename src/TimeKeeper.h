#pragma once

#include <Arduino.h>
#include <time.h>

#include "AppConfig.h"

class TimeKeeper {
public:
  void begin(const AppConfig &cfg);
  void tick(const AppConfig &cfg);

  bool isTimeValid() const;
  time_t nowUtc() const;
  time_t nowLocal(const AppConfig &cfg) const;
  int32_t localOffsetSeconds(const AppConfig &cfg) const;
  bool dstActive(const AppConfig &cfg) const;
  time_t nextDstChangeUtc(const AppConfig &cfg) const;
  time_t nextDstChangeLocal(const AppConfig &cfg) const;

  void setManualUtc(time_t epochUtc);
  bool syncNtpNow(const AppConfig &cfg);

  time_t lastNtpSyncUtc() const;
  time_t lastManualSetUtc() const;
  String timeSource() const; // "invalid" | "manual" | "ntp"
  bool lastNtpAttemptFailed() const;

 private:
  bool _ntpConfigured = false;
  uint32_t _lastNtpAttemptMs = 0;
  time_t _lastNtpSyncUtc = 0;
  time_t _lastManualSetUtc = 0;
  bool _lastNtpAttemptFailed = false;
};
