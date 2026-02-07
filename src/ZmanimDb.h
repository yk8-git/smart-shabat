#pragma once

#include <Arduino.h>

struct ZmanimMeta {
  bool ok = false;
  uint8_t kind = 0; // 1 = date-key DB, 2 = month/day template (all years)
  uint32_t count = 0;
  uint32_t firstDateKey = 0;
  uint32_t lastDateKey = 0;
  String lastError;
};

class ZmanimDb {
public:
  void begin();

  bool hasData() const;
  ZmanimMeta meta() const;

  // Returns candles + havdalah (minutes-from-midnight).
  bool getForDate(uint32_t dateKey, uint16_t &candlesMinutes, uint16_t &havdalahMinutes) const;

  static String formatDateKey(uint32_t dateKey);

private:
  void clearTemplateMemory();
  bool loadEmbeddedTemplateIntoMemory();

  mutable ZmanimMeta _meta;
  bool _templateLoaded = false;
  bool _hasMd[13][32] = {};
  uint16_t _mdCandles[13][32] = {};
  uint16_t _mdHavdalah[13][32] = {};
};
