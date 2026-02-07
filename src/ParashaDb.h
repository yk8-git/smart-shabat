#pragma once

#include <Arduino.h>

struct ParashaMeta {
  bool ok = false;
  uint32_t count = 0;
  uint32_t firstDateKey = 0;
  uint32_t lastDateKey = 0;
  bool israel = true;
  uint16_t startYear = 0;
  uint16_t years = 0;
  String lastError;
};

class ParashaDb {
public:
  void begin();

  bool hasData() const;
  ParashaMeta meta() const;

  bool getName(uint32_t dateKey, String &outName) const;
  static String formatDateKey(uint32_t dateKey);

private:
  ParashaMeta _meta;
};

