#include "TimeKeeper.h"

#include <ESP8266WiFi.h>
#include <sys/time.h>
#include <time.h>

#include "DateMath.h"

namespace {
constexpr uint32_t kNtpRetryMs = 60UL * 1000UL;
constexpr time_t kMinValidEpoch = 1704067200; // 2024-01-01

uint16_t yearFromLocalEpoch(time_t localEpoch) {
  tm t{};
  gmtime_r(&localEpoch, &t);
  return static_cast<uint16_t>(t.tm_year + 1900);
}

uint32_t lastSundayOfMonth(uint16_t year, uint8_t month, uint8_t lastDay) {
  uint32_t key = static_cast<uint32_t>(year) * 10000UL + static_cast<uint32_t>(month) * 100UL + lastDay;
  while (datemath::weekday(key) != 0) {
    key = datemath::addDays(key, -1);
  }
  return key;
}

// Israel DST (rule-based, not timezone database):
// - Starts: Friday before last Sunday of March, at 02:00 (standard time)
// - Ends: last Sunday of October, at 02:00 (daylight time)
// Returns false when the rule isn't applicable (non-Israel base offset).
bool israelDstTransitionsUtc(uint16_t year,
                             int32_t tzOffsetSeconds,
                             int32_t dstOffsetSeconds,
                             time_t &outStartUtc,
                             time_t &outEndUtc) {
  if (dstOffsetSeconds <= 0) return false;
  if (tzOffsetSeconds != 120 * 60) return false; // this firmware ships with Israel zmanim

  const uint32_t lastSunMar = lastSundayOfMonth(year, 3, 31);
  const uint32_t startDate = datemath::addDays(lastSunMar, -2); // Friday
  const uint32_t lastSunOct = lastSundayOfMonth(year, 10, 31);

  const int64_t startLocal = datemath::localEpochFromDateKeyMinutes(startDate, 2 * 60);
  const int64_t endLocal = datemath::localEpochFromDateKeyMinutes(lastSunOct, 2 * 60);

  outStartUtc = static_cast<time_t>(startLocal - tzOffsetSeconds);
  outEndUtc = static_cast<time_t>(endLocal - (tzOffsetSeconds + dstOffsetSeconds));
  return outEndUtc > outStartUtc;
}

bool isDstActiveAtUtc(const AppConfig &cfg, time_t utc) {
  if (cfg.dstMode == 0) return false;
  if (cfg.dstMode == 2) return cfg.dstEnabled; // manual
  // auto
  const int32_t tz = static_cast<int32_t>(cfg.tzOffsetMinutes) * 60;
  const int32_t dst = static_cast<int32_t>(cfg.dstOffsetMinutes) * 60;

  const time_t stdLocal = utc + tz;
  const uint16_t year = yearFromLocalEpoch(stdLocal);
  time_t startUtc = 0, endUtc = 0;
  if (!israelDstTransitionsUtc(year, tz, dst, startUtc, endUtc)) return false;
  return utc >= startUtc && utc < endUtc;
}

time_t nextDstChangeUtc(const AppConfig &cfg, time_t utc) {
  if (cfg.dstMode != 1) return 0; // only auto has a meaningful "next change"
  const int32_t tz = static_cast<int32_t>(cfg.tzOffsetMinutes) * 60;
  const int32_t dst = static_cast<int32_t>(cfg.dstOffsetMinutes) * 60;
  if (dst <= 0) return 0;

  const time_t stdLocal = utc + tz;
  uint16_t year = yearFromLocalEpoch(stdLocal);

  time_t startUtc = 0, endUtc = 0;
  if (!israelDstTransitionsUtc(year, tz, dst, startUtc, endUtc)) return 0;

  if (utc < startUtc) return startUtc;
  if (utc >= startUtc && utc < endUtc) return endUtc;

  // After end: next year's start
  year = static_cast<uint16_t>(year + 1);
  if (!israelDstTransitionsUtc(year, tz, dst, startUtc, endUtc)) return 0;
  return startUtc;
}
} // namespace

void TimeKeeper::begin(const AppConfig &cfg) {
  _ntpConfigured = false;
  _lastNtpAttemptMs = 0;
  _lastNtpSyncUtc = 0;
  _lastManualSetUtc = 0;
  if (cfg.ntpEnabled) {
    syncNtpNow(cfg);
  }
}

void TimeKeeper::tick(const AppConfig &cfg) {
  if (!cfg.ntpEnabled) return;
  const bool valid = isTimeValid();

  if (!valid) {
    if (millis() - _lastNtpAttemptMs < kNtpRetryMs) return;
    syncNtpNow(cfg);
    return;
  }

  if (cfg.ntpResyncMinutes == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  const time_t now = nowUtc();
  const time_t interval = static_cast<time_t>(cfg.ntpResyncMinutes) * 60;
  if (interval <= 0) return;

  const time_t baseline = (_lastNtpSyncUtc > _lastManualSetUtc) ? _lastNtpSyncUtc : _lastManualSetUtc;
  if (baseline == 0) return;
  if ((now - baseline) < interval) return;
  if (millis() - _lastNtpAttemptMs < kNtpRetryMs) return;
  syncNtpNow(cfg);
}

bool TimeKeeper::isTimeValid() const {
  const time_t now = time(nullptr);
  return now >= kMinValidEpoch;
}

time_t TimeKeeper::nowUtc() const { return time(nullptr); }

int32_t TimeKeeper::localOffsetSeconds(const AppConfig &cfg) const {
  const bool dstActive = isDstActiveAtUtc(cfg, nowUtc());
  const int dst = dstActive ? cfg.dstOffsetMinutes : 0;
  return static_cast<int32_t>((cfg.tzOffsetMinutes + dst) * 60);
}

time_t TimeKeeper::nowLocal(const AppConfig &cfg) const { return nowUtc() + localOffsetSeconds(cfg); }

bool TimeKeeper::dstActive(const AppConfig &cfg) const { return isDstActiveAtUtc(cfg, nowUtc()); }

time_t TimeKeeper::nextDstChangeUtc(const AppConfig &cfg) const { return ::nextDstChangeUtc(cfg, nowUtc()); }

time_t TimeKeeper::nextDstChangeLocal(const AppConfig &cfg) const {
  const time_t utc = nowUtc();
  if (cfg.dstMode != 1) return 0;

  const int32_t tz = static_cast<int32_t>(cfg.tzOffsetMinutes) * 60;
  const int32_t dst = static_cast<int32_t>(cfg.dstOffsetMinutes) * 60;
  if (dst <= 0) return 0;

  const time_t stdLocal = utc + tz;
  uint16_t year = yearFromLocalEpoch(stdLocal);

  time_t startUtc = 0, endUtc = 0;
  if (!israelDstTransitionsUtc(year, tz, dst, startUtc, endUtc)) return 0;

  const uint32_t startDate = datemath::addDays(lastSundayOfMonth(year, 3, 31), -2);
  const uint32_t endDate = lastSundayOfMonth(year, 10, 31);

  if (utc < startUtc) return static_cast<time_t>(datemath::localEpochFromDateKeyMinutes(startDate, 2 * 60));
  if (utc >= startUtc && utc < endUtc) return static_cast<time_t>(datemath::localEpochFromDateKeyMinutes(endDate, 2 * 60));

  year = static_cast<uint16_t>(year + 1);
  if (!israelDstTransitionsUtc(year, tz, dst, startUtc, endUtc)) return 0;
  const uint32_t startDate2 = datemath::addDays(lastSundayOfMonth(year, 3, 31), -2);
  return static_cast<time_t>(datemath::localEpochFromDateKeyMinutes(startDate2, 2 * 60));
}

void TimeKeeper::setManualUtc(time_t epochUtc) {
  timeval tv{};
  tv.tv_sec = epochUtc;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  _lastManualSetUtc = epochUtc;
}

bool TimeKeeper::syncNtpNow(const AppConfig &cfg) {
  _lastNtpAttemptMs = millis();
  if (WiFi.status() != WL_CONNECTED) return false;

  configTime(0, 0, cfg.ntpServer.c_str());
  _ntpConfigured = true;

  bool ok = false;
  for (int i = 0; i < 20; i += 1) {
    if (isTimeValid()) {
      ok = true;
      break;
    }
    delay(250);
    yield();
  }
  if (!ok) ok = isTimeValid();
  if (ok) {
    _lastNtpSyncUtc = nowUtc();
    Serial.printf("[ntp] synced utc=%lu server=%s\n", static_cast<unsigned long>(_lastNtpSyncUtc), cfg.ntpServer.c_str());
  } else {
    Serial.printf("[ntp] failed server=%s\n", cfg.ntpServer.c_str());
  }
  return ok;
}

time_t TimeKeeper::lastNtpSyncUtc() const { return _lastNtpSyncUtc; }

time_t TimeKeeper::lastManualSetUtc() const { return _lastManualSetUtc; }

String TimeKeeper::timeSource() const {
  if (!isTimeValid()) return "invalid";
  if (_lastNtpSyncUtc != 0 && _lastNtpSyncUtc >= _lastManualSetUtc) return "ntp";
  return (_lastManualSetUtc != 0) ? "manual" : "manual";
}
