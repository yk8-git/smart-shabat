#include "StatusIndicator.h"

void StatusIndicator::begin(const AppConfig &cfg) {
  _cycleStartMs = millis();
  _mode = IndicatorMode::Ok;
  applyConfig(cfg);
  writeLed(false);
}

void StatusIndicator::applyConfig(const AppConfig &cfg) {
  _gpio = cfg.statusLedGpio;
  _activeLow = cfg.statusLedActiveLow;
  if (_gpio >= 0) {
    pinMode(_gpio, OUTPUT);
  }
  writeLed(false);
}

void StatusIndicator::setMode(IndicatorMode mode) {
  if (_mode == mode) return;
  _mode = mode;
  _cycleStartMs = millis();
}

void StatusIndicator::tick() {
  if (_gpio < 0) return;
  const uint32_t elapsed = millis() - _cycleStartMs;
  const bool shouldOn = patternOn(elapsed);
  if (shouldOn == _ledOn) return;
  writeLed(shouldOn);
}

void StatusIndicator::writeLed(bool on) {
  _ledOn = on;
  if (_gpio < 0) return;
  const bool level = _activeLow ? !on : on;
  digitalWrite(_gpio, level ? HIGH : LOW);
}

// Returns true if LED should be ON at elapsedMs into cycle.
bool StatusIndicator::patternOn(uint32_t elapsedMs) const {
  switch (_mode) {
  case IndicatorMode::Ok: {
    // One short blink every 5 seconds
    const uint32_t t = elapsedMs % 5000;
    return t < 60;
  }
  case IndicatorMode::ApMode: {
    // Double blink every 2 seconds: 100ms on, 150ms off, 100ms on
    const uint32_t t = elapsedMs % 2000;
    if (t < 100) return true;
    if (t < 250) return false;
    if (t < 350) return true;
    return false;
  }
  case IndicatorMode::TimeInvalid: {
    // Fast blink ~5Hz
    const uint32_t t = elapsedMs % 200;
    return t < 100;
  }
  case IndicatorMode::MissingZmanim: {
    // Triple blink every 2 seconds
    const uint32_t t = elapsedMs % 2000;
    if (t < 100) return true;
    if (t < 220) return false;
    if (t < 320) return true;
    if (t < 440) return false;
    if (t < 540) return true;
    return false;
  }
  case IndicatorMode::MissingHolidays: {
    // Slow blink every 3 seconds
    const uint32_t t = elapsedMs % 3000;
    return t < 120;
  }
  case IndicatorMode::WaitingNtp: {
    // Two slow blinks every 4 seconds
    const uint32_t t = elapsedMs % 4000;
    if (t < 120) return true;
    if (t < 420) return false;
    if (t < 540) return true;
    return false;
  }
  case IndicatorMode::NtpStale: {
    // Slow 1Hz blink
    const uint32_t t = elapsedMs % 1000;
    return t < 120;
  }
  default:
    break;
  }
  return false;
}
