#pragma once

#include <Arduino.h>

#include "AppConfig.h"

enum class IndicatorMode : uint8_t {
  Ok = 0,
  ApMode,
  TimeInvalid,
  MissingZmanim,
  MissingHolidays,
  WaitingNtp,
  NtpStale,
};

class StatusIndicator {
public:
  void begin(const AppConfig &cfg);
  void applyConfig(const AppConfig &cfg);
  void setMode(IndicatorMode mode);
  void tick();

private:
  int _gpio = -1;
  bool _activeLow = true;
  IndicatorMode _mode = IndicatorMode::Ok;

  uint32_t _cycleStartMs = 0;
  bool _ledOn = false;

  void writeLed(bool on);
  bool patternOn(uint32_t elapsedMs) const;
};

