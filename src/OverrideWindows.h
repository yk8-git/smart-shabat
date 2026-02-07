#pragma once

#include <Arduino.h>

#include "AppConfig.h"

struct ActiveWindowOverride {
  bool active = false;
  bool stateOn = false;
  uint32_t startUtc = 0;
  uint32_t endUtc = 0;
};

// Returns the active override (if any). If multiple match, picks the one with the latest startUtc.
ActiveWindowOverride overridesFindActive(const AppConfig &cfg, uint32_t nowUtc);

// Computes effective relay state given a base state.
bool overridesApply(const AppConfig &cfg, uint32_t nowUtc, bool baseStateOn, bool &outStateOn, ActiveWindowOverride &outActive);

