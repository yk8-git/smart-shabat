#pragma once

#include <Arduino.h>

#include "AppConfig.h"
#include "HolidayDb.h"
#include "ParashaDb.h"
#include "TimeKeeper.h"
#include "ZmanimDb.h"

struct ScheduleStatus {
  bool ok = false;
  bool inHolyTime = false;
  bool hasZmanim = false;
  bool hasHolidays = false;
  String errorCode; // stable codes for UI (e.g., CLOCK_NOT_SET, MISSING_ZMANIM)
  String error;

  int64_t nowLocal = 0;
  int64_t nextChangeLocal = 0; // local epoch seconds
  bool nextStateOn = false;
};

class ScheduleEngine {
public:
  void begin(ZmanimDb &zmanim, HolidayDb &holidays, ParashaDb &parasha);

  void tick(const AppConfig &cfg, const TimeKeeper &time);
  void invalidate();

  bool desiredRelayOn() const;
  ScheduleStatus status() const;

  // For UI: JSON array of upcoming windows (start,end,label)
  String upcomingJson(uint16_t limit) const;

private:
  struct Window {
    int64_t startLocal = 0;
    int64_t endLocal = 0;
    uint8_t kind = 0; // 1=Shabbat, 2=Holiday (bitset)
  };

  ZmanimDb *_zmanim = nullptr;
  HolidayDb *_holidays = nullptr;
  ParashaDb *_parasha = nullptr;

  Window _windows[64];
  uint8_t _windowCount = 0;
  uint8_t _index = 0;

  uint32_t _builtForDateKey = 0;
  uint32_t _lastConfigSig = 0;
  uint32_t _lastBuildMs = 0;
  String _lastError;

  bool _desiredOn = false;
  ScheduleStatus _status;

  uint32_t configSig(const AppConfig &cfg) const;
  uint32_t dateKeyFromLocalEpoch(time_t localEpoch) const;

  void rebuild(const AppConfig &cfg, time_t nowLocalEpoch);
  void mergeWindows();
};
