#include "StatusIndicator.h"

void StatusIndicator::begin(const AppConfig &cfg) {
  _cycleStartMs = millis();
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

void StatusIndicator::setErrorCode(uint8_t code) {
  if (_errorCode == code) return;
  _errorCode = code;
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
  if (_errorCode == kTimeInvalidCode) {
    // Continuous slow blink (time not set)
    const uint32_t cycleMs = 1000;
    const uint32_t blinkWidthMs = 500;
    const uint32_t t = elapsedMs % cycleMs;
    return t < blinkWidthMs;
  }

  if (_errorCode == 0) {
    return false;
  }

  const uint8_t count = (_errorCode > 3) ? 3 : _errorCode;
  // Visible blink groups: N blinks, then a pause.
  const uint32_t cycleMs = 6000;
  const uint32_t intervalMs = 700;
  const uint32_t blinkWidthMs = 300;
  const uint32_t t = elapsedMs % cycleMs;
  const uint32_t activeWindow = static_cast<uint32_t>(count) * intervalMs;
  if (t >= activeWindow) return false;
  const uint32_t pos = t % intervalMs;
  return pos < blinkWidthMs;
}
