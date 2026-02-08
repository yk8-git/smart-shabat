#pragma once

#include <Arduino.h>

#include "AppConfig.h"

class StatusIndicator {
public:
  static constexpr uint8_t kTimeInvalidCode = 0xFF;
  void begin(const AppConfig &cfg);
  void applyConfig(const AppConfig &cfg);
  void setErrorCode(uint8_t code);
  void tick();

private:
  int _gpio = -1;
  bool _activeLow = true;
  uint8_t _errorCode = 0;

  uint32_t _cycleStartMs = 0;
  bool _ledOn = false;

  void writeLed(bool on);
  bool patternOn(uint32_t elapsedMs) const;
};
