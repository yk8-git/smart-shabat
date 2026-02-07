#include "ZmanimDb.h"

#include <pgmspace.h>

#include "EmbeddedZmanim.h"

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
} // namespace

void ZmanimDb::clearTemplateMemory() {
  _templateLoaded = false;
  for (int m = 0; m < 13; m += 1) {
    for (int d = 0; d < 32; d += 1) {
      _hasMd[m][d] = false;
      _mdCandles[m][d] = 0;
      _mdHavdalah[m][d] = 0;
    }
  }
}

bool ZmanimDb::loadEmbeddedTemplateIntoMemory() {
  clearTemplateMemory();

  uint32_t loaded = 0;
  for (uint16_t i = 0; i < (sizeof(kEmbeddedZmanim) / sizeof(kEmbeddedZmanim[0])); i += 1) {
    EmbeddedZmanimEntry e{};
    memcpy_P(&e, &kEmbeddedZmanim[i], sizeof(e));
    if (e.month < 1 || e.month > 12) continue;
    if (e.day < 1 || e.day > 31) continue;
    _hasMd[e.month][e.day] = true;
    _mdCandles[e.month][e.day] = e.candlesMinutes;
    _mdHavdalah[e.month][e.day] = e.havdalahMinutes;
    loaded += 1;
  }

  _templateLoaded = loaded > 0;
  _meta.ok = _templateLoaded;
  _meta.kind = 2;
  _meta.count = loaded;
  _meta.firstDateKey = 0;
  _meta.lastDateKey = 0;
  _meta.lastError = _templateLoaded ? "" : "missing embedded zmanim";
  return _templateLoaded;
}

void ZmanimDb::begin() {
  _meta = {};
  clearTemplateMemory();
  loadEmbeddedTemplateIntoMemory();
}

bool ZmanimDb::hasData() const { return _meta.ok && _meta.count > 0 && _templateLoaded; }

ZmanimMeta ZmanimDb::meta() const { return _meta; }

String ZmanimDb::formatDateKey(uint32_t dateKey) { return dateKeyToString(dateKey); }

bool ZmanimDb::getForDate(uint32_t dateKey, uint16_t &candlesMinutes, uint16_t &havdalahMinutes) const {
  if (!hasData()) return false;
  const uint8_t month = static_cast<uint8_t>((dateKey / 100UL) % 100UL);
  const uint8_t day = static_cast<uint8_t>(dateKey % 100UL);
  if (month < 1 || month > 12 || day < 1 || day > 31) return false;
  if (!_hasMd[month][day]) return false;
  candlesMinutes = _mdCandles[month][day];
  havdalahMinutes = _mdHavdalah[month][day];
  return true;
}
