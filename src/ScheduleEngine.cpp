#include "ScheduleEngine.h"

#include <ArduinoJson.h>
#include <time.h>

#include "DateMath.h"

namespace {
constexpr uint32_t kRebuildThrottleMs = 30UL * 1000UL;
constexpr uint32_t kPeriodicRebuildMs = 6UL * 60UL * 60UL * 1000UL;
constexpr int kLookaheadDays = 70;

uint32_t fnv1a32(const uint8_t *data, size_t len) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < len; i += 1) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t clampMinutes(int minutes) {
  if (minutes < 0) return 0;
  if (minutes > 1439) return 1439;
  return static_cast<uint32_t>(minutes);
}

uint32_t lastSundayOfMonth(uint16_t year, uint8_t month, uint8_t lastDay) {
  uint32_t key = static_cast<uint32_t>(year) * 10000UL + static_cast<uint32_t>(month) * 100UL + lastDay;
  while (datemath::weekday(key) != 0) {
    key = datemath::addDays(key, -1);
  }
  return key;
}

int dstShiftMinutesForDateKey(const AppConfig &cfg, uint32_t dateKey) {
  if (cfg.dstMode == 0) return 0;
  if (cfg.dstOffsetMinutes <= 0) return 0;
  if (cfg.dstMode == 2) return cfg.dstEnabled ? cfg.dstOffsetMinutes : 0;

  // Auto DST: Israel rules only (this firmware ships with Israel zmanim).
  if (cfg.tzOffsetMinutes != 120) return 0;

  const uint16_t year = static_cast<uint16_t>(dateKey / 10000UL);
  const uint32_t startDate = datemath::addDays(lastSundayOfMonth(year, 3, 31), -2); // Friday
  const uint32_t endDate = lastSundayOfMonth(year, 10, 31);                         // Sunday
  return (dateKey >= startDate && dateKey < endDate) ? cfg.dstOffsetMinutes : 0;
}
} // namespace

void ScheduleEngine::begin(ZmanimDb &zmanim, HolidayDb &holidays, ParashaDb &parasha) {
  _zmanim = &zmanim;
  _holidays = &holidays;
  _parasha = &parasha;
  _windowCount = 0;
  _index = 0;
  _builtForDateKey = 0;
  _lastConfigSig = 0;
  _lastBuildMs = 0;
  _desiredOn = false;
  _status = {};
  _lastError = "";
}

void ScheduleEngine::invalidate() { _windowCount = 0; }

uint32_t ScheduleEngine::configSig(const AppConfig &cfg) const {
  struct {
    int minutesBeforeShkia;
    int minutesAfterTzeit;
    int tzOffsetMinutes;
    uint8_t dstMode;
    bool dstEnabled;
    int dstOffsetMinutes;
    bool israel;
  } packed{};

  packed.minutesBeforeShkia = cfg.minutesBeforeShkia;
  packed.minutesAfterTzeit = cfg.minutesAfterTzeit;
  packed.tzOffsetMinutes = cfg.tzOffsetMinutes;
  packed.dstMode = cfg.dstMode;
  packed.dstEnabled = cfg.dstEnabled;
  packed.dstOffsetMinutes = cfg.dstOffsetMinutes;
  packed.israel = cfg.israel;

  return fnv1a32(reinterpret_cast<const uint8_t *>(&packed), sizeof(packed));
}

uint32_t ScheduleEngine::dateKeyFromLocalEpoch(time_t localEpoch) const {
  tm t{};
  gmtime_r(&localEpoch, &t);
  const int y = t.tm_year + 1900;
  const int m = t.tm_mon + 1;
  const int d = t.tm_mday;
  return static_cast<uint32_t>(y * 10000 + m * 100 + d);
}

void ScheduleEngine::tick(const AppConfig &cfg, const TimeKeeper &time) {
  _status = {};
  _status.hasZmanim = _zmanim && _zmanim->hasData();
  _status.hasHolidays = _holidays && _holidays->hasData();
  _status.errorCode = "";
  _status.error = _lastError;

  if (!time.isTimeValid()) {
    _desiredOn = false;
    _status.ok = false;
    _status.errorCode = "CLOCK_NOT_SET";
    _status.error = "clock not set";
    return;
  }

  if (!_status.hasZmanim) {
    _desiredOn = false;
    _status.ok = false;
    _status.errorCode = "MISSING_ZMANIM";
    _status.error = "missing zmanim data";
    return;
  }

  const time_t nowLocalEpoch = time.nowLocal(cfg);
  _status.nowLocal = static_cast<int64_t>(nowLocalEpoch);

  const uint32_t todayKey = dateKeyFromLocalEpoch(nowLocalEpoch);
  const uint32_t sig = configSig(cfg);

  const bool shouldRebuild = (todayKey != _builtForDateKey) || (sig != _lastConfigSig) || (_windowCount == 0) ||
                             (millis() - _lastBuildMs > kPeriodicRebuildMs);

  if (shouldRebuild && (millis() - _lastBuildMs) >= kRebuildThrottleMs) {
    rebuild(cfg, nowLocalEpoch);
  }

  // Advance index
  while (_index < _windowCount && nowLocalEpoch >= _windows[_index].endLocal) {
    _index += 1;
  }

  const bool inWindow = (_index < _windowCount && nowLocalEpoch >= _windows[_index].startLocal &&
                         nowLocalEpoch < _windows[_index].endLocal);

  _desiredOn = inWindow;
  _status.ok = true;
  _status.inHolyTime = inWindow;

  if (_index < _windowCount) {
    if (inWindow) {
      _status.nextChangeLocal = _windows[_index].endLocal;
      _status.nextStateOn = false;
    } else {
      _status.nextChangeLocal = _windows[_index].startLocal;
      _status.nextStateOn = true;
    }
  } else {
    _status.nextChangeLocal = 0;
    _status.nextStateOn = false;
  }
}

void ScheduleEngine::rebuild(const AppConfig &cfg, time_t nowLocalEpoch) {
  _windowCount = 0;
  _index = 0;
  _lastBuildMs = millis();
  _builtForDateKey = dateKeyFromLocalEpoch(nowLocalEpoch);
  _lastConfigSig = configSig(cfg);

  String firstError = "";

  const uint32_t startKey = datemath::addDays(_builtForDateKey, -3);
  const uint32_t endKey = datemath::addDays(_builtForDateKey, kLookaheadDays);

  uint32_t dateKey = startKey;
  while (true) {
    const int wd = datemath::weekday(dateKey);
    const bool isShabbat = (wd == 6);
    const bool isHoliday = _holidays && _holidays->hasData() && _holidays->isYomTovDate(dateKey);

    const uint8_t kind = (isShabbat ? 1 : 0) | (isHoliday ? 2 : 0);
    if (kind != 0 && _windowCount < (sizeof(_windows) / sizeof(_windows[0]))) {
      const uint32_t prevKey = datemath::addDays(dateKey, -1);

      uint16_t candlesPrev = 0;
      uint16_t havdalah = 0;
      uint16_t dummy = 0;

      if (!_zmanim->getForDate(prevKey, candlesPrev, dummy) || !_zmanim->getForDate(dateKey, dummy, havdalah)) {
        if (!firstError.length()) {
          firstError = "missing zmanim around " + ZmanimDb::formatDateKey(dateKey);
        }
      } else {
        // Zmanim are stored in standard time (UTC+2). Apply DST shift per date when enabled.
        const int dstPrev = dstShiftMinutesForDateKey(cfg, prevKey);
        const int dstCur = dstShiftMinutesForDateKey(cfg, dateKey);

        // Reference times:
        // - Start: hadlakat nerot (candles) on the eve (prevKey), minus optional extra minutes.
        // - End: motzaei Shabbat/YomTov (havdalah) on dateKey, plus optional extra minutes.
        const int startBase = static_cast<int>(candlesPrev) + dstPrev;
        const int endBase = static_cast<int>(havdalah) + dstCur;

        const int startMin = startBase - cfg.minutesBeforeShkia;
        const int endMin = endBase + cfg.minutesAfterTzeit;
        const int64_t startLocal = datemath::localEpochFromDateKeyMinutes(prevKey, clampMinutes(startMin));
        const int64_t endLocal = datemath::localEpochFromDateKeyMinutes(dateKey, clampMinutes(endMin));
        if (endLocal > startLocal) {
          Window &w = _windows[_windowCount];
          w.startLocal = startLocal;
          w.endLocal = endLocal;
          w.kind = kind;
          _windowCount += 1;
        }
      }
    }

    if (dateKey == endKey) break;
    dateKey = datemath::addDays(dateKey, 1);
    if (dateKey == startKey) break; // safety (shouldn't happen)
  }

  // Sort windows by startLocal (simple insertion sort, small N)
  for (uint8_t i = 1; i < _windowCount; i += 1) {
    Window keyW = _windows[i];
    int j = static_cast<int>(i) - 1;
    while (j >= 0 && _windows[j].startLocal > keyW.startLocal) {
      _windows[j + 1] = _windows[j];
      j -= 1;
    }
    _windows[j + 1] = keyW;
  }

  mergeWindows();

  _lastError = firstError;
}

void ScheduleEngine::mergeWindows() {
  if (_windowCount <= 1) return;

  Window merged[64];
  uint8_t outCount = 0;

  merged[0] = _windows[0];
  outCount = 1;

  for (uint8_t i = 1; i < _windowCount; i += 1) {
    Window &cur = merged[outCount - 1];
    const Window &next = _windows[i];

    if (next.startLocal <= cur.endLocal) {
      if (next.endLocal > cur.endLocal) cur.endLocal = next.endLocal;
      cur.kind |= next.kind;
      continue;
    }

    if (outCount < (sizeof(merged) / sizeof(merged[0]))) {
      merged[outCount] = next;
      outCount += 1;
    }
  }

  for (uint8_t i = 0; i < outCount; i += 1) _windows[i] = merged[i];
  _windowCount = outCount;
}

bool ScheduleEngine::desiredRelayOn() const { return _desiredOn; }

ScheduleStatus ScheduleEngine::status() const { return _status; }

String ScheduleEngine::upcomingJson(uint16_t limit) const {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();

  uint16_t added = 0;
  for (uint8_t i = _index; i < _windowCount && added < limit; i += 1) {
    JsonObject o = arr.createNestedObject();
    o["startLocal"] = _windows[i].startLocal;
    o["endLocal"] = _windows[i].endLocal;
    o["kind"] = _windows[i].kind;
    const String label = (_windows[i].kind == 1) ? "שבת" : (_windows[i].kind == 2) ? "חג" : "שבת/חג";
    o["label"] = label;

    // Add a friendly title (holiday name / parasha) for peace of mind.
    String title = "";
    String lastAdded = "";
    const uint32_t startKey = dateKeyFromLocalEpoch(static_cast<time_t>(_windows[i].startLocal));
    const uint32_t endKey = dateKeyFromLocalEpoch(static_cast<time_t>(_windows[i].endLocal));
    uint32_t dk = startKey;
    uint8_t steps = 0;
    while (true) {
      const bool isHoliday = _holidays && _holidays->hasData() && _holidays->isYomTovDate(dk);
      const bool isShabbat = (datemath::weekday(dk) == 6);

      String name = "";
      if (isHoliday) {
        if (_holidays) _holidays->getYomTovName(dk, name);
      } else if (isShabbat) {
        if (_parasha) _parasha->getName(dk, name);
      }

      name.trim();
      if (name.length() && name != lastAdded) {
        if (title.length()) title += " · ";
        title += name;
        lastAdded = name;
      }

      if (dk == endKey) break;
      dk = datemath::addDays(dk, 1);
      steps += 1;
      if (steps > 10 || dk == startKey) break;
    }

    if (title.length()) {
      o["title"] = title;
    }
    added += 1;
  }

  String out;
  serializeJson(doc, out);
  return out;
}
