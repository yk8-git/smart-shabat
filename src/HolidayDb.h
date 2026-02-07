#pragma once

#include <Arduino.h>

struct HolidayMeta {
  bool ok = false;
  uint32_t count = 0;
  uint32_t firstDateKey = 0;
  uint32_t lastDateKey = 0;
  bool israel = true;
  uint16_t startYear = 0;
  uint16_t years = 0;
  String lastError;
};

class HolidayDb {
public:
  void begin();

  bool hasData() const;
  HolidayMeta meta() const;

  bool isYomTovDate(uint32_t dateKey) const;
  bool getYomTovName(uint32_t dateKey, String &outName) const;
  static String formatDateKey(uint32_t dateKey);

  HolidayMeta _meta;
};
