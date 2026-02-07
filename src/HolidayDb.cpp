#include "HolidayDb.h"

#include <pgmspace.h>

#include "EmbeddedHolidays.h"

namespace {
String dateKeyToString(uint32_t key) {
  const uint32_t y = key / 10000UL;
  const uint32_t m = (key / 100UL) % 100UL;
  const uint32_t d = key % 100UL;
  char buf[16];
  snprintf(buf,
           sizeof(buf),
           "%04lu-%02lu-%02lu",
           static_cast<unsigned long>(y),
           static_cast<unsigned long>(m),
           static_cast<unsigned long>(d));
  return String(buf);
}

uint32_t readDateKey(uint32_t index) {
  return pgm_read_dword(&embedded_holidays::kEntries[index].dateKey);
}

uint16_t readNameOffset(uint32_t index) {
  return pgm_read_word(&embedded_holidays::kEntries[index].nameOffset);
}
} // namespace

void HolidayDb::begin() {
  _meta = {};
  _meta.ok = embedded_holidays::kCount > 0;
  _meta.count = embedded_holidays::kCount;
  _meta.israel = embedded_holidays::kIsrael;
  _meta.startYear = embedded_holidays::kStartYear;
  _meta.years = embedded_holidays::kYears;
  _meta.firstDateKey = embedded_holidays::kCount ? readDateKey(0) : 0;
  _meta.lastDateKey = embedded_holidays::kCount ? readDateKey(embedded_holidays::kCount - 1) : 0;
  _meta.lastError = _meta.ok ? "" : "missing embedded holidays";
}

HolidayMeta HolidayDb::meta() const { return _meta; }

bool HolidayDb::hasData() const { return _meta.ok && _meta.count > 0; }

String HolidayDb::formatDateKey(uint32_t dateKey) { return dateKeyToString(dateKey); }

bool HolidayDb::isYomTovDate(uint32_t dateKey) const {
  if (!hasData()) return false;
  if (dateKey < _meta.firstDateKey || dateKey > _meta.lastDateKey) return false;

  uint32_t lo = 0;
  uint32_t hi = embedded_holidays::kCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    const uint32_t midKey = readDateKey(mid);
    if (midKey < dateKey) lo = mid + 1;
    else hi = mid;
  }

  if (lo >= embedded_holidays::kCount) return false;
  return readDateKey(lo) == dateKey;
}

bool HolidayDb::getYomTovName(uint32_t dateKey, String &outName) const {
  outName = "";
  if (!hasData()) return false;
  if (dateKey < _meta.firstDateKey || dateKey > _meta.lastDateKey) return false;

  uint32_t lo = 0;
  uint32_t hi = embedded_holidays::kCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    const uint32_t midKey = readDateKey(mid);
    if (midKey < dateKey) lo = mid + 1;
    else hi = mid;
  }

  if (lo >= embedded_holidays::kCount) return false;
  if (readDateKey(lo) != dateKey) return false;

  const uint16_t off = readNameOffset(lo);
  char buf[64];
  strncpy_P(buf, embedded_holidays::kNames + off, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  outName = String(buf);
  return outName.length() > 0;
}
