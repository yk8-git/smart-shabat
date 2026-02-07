#include "RelayController.h"

void RelayController::begin(const AppConfig &cfg, bool initialOn) {
  _isOn = initialOn;
  applyConfig(cfg);
}

void RelayController::applyConfig(const AppConfig &cfg) {
  if (_gpio != cfg.relayGpio) {
    _gpio = cfg.relayGpio;
    pinMode(_gpio, OUTPUT);
  }
  _activeLow = cfg.relayActiveLow;
  writePin(_isOn);
}

void RelayController::writePin(bool on) {
  if (_gpio < 0) return;
  if (_activeLow) {
    digitalWrite(_gpio, on ? LOW : HIGH);
  } else {
    digitalWrite(_gpio, on ? HIGH : LOW);
  }
}

void RelayController::setOn(bool on) {
  if (_isOn == on) return;
  _isOn = on;
  writePin(on);
}

bool RelayController::isOn() const { return _isOn; }
