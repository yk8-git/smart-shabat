#pragma once

#include <Arduino.h>

#include "AppConfig.h"

class RelayController {
public:
  void begin(const AppConfig &cfg, bool initialOn);
  void applyConfig(const AppConfig &cfg);

  void setOn(bool on);
  bool isOn() const;

private:
  int _gpio = -1;
  bool _activeLow = true;
  bool _isOn = false;
  void writePin(bool on);
};
