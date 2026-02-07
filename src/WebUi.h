#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>

#include "AppConfig.h"
#include "RelayController.h"
#include "ScheduleEngine.h"
#include "TimeKeeper.h"
#include "WifiController.h"
#include "HolidayDb.h"
#include "HistoryLog.h"
#include "OtaUpdater.h"
#include "StatusIndicator.h"
#include "ZmanimDb.h"

class WebUi {
public:
  explicit WebUi(uint16_t port = 80);

  void begin(AppConfig &cfg,
             WifiController &wifi,
             TimeKeeper &time,
             RelayController &relay,
             ZmanimDb &zmanim,
             HolidayDb &holidays,
             ScheduleEngine &schedule,
             OtaUpdater &ota,
             StatusIndicator &indicator,
             HistoryLog &history);
  void tick();

private:
  ESP8266WebServer _server;

  AppConfig *_cfg = nullptr;
  WifiController *_wifi = nullptr;
  TimeKeeper *_time = nullptr;
  RelayController *_relay = nullptr;
  ZmanimDb *_zmanim = nullptr;
  HolidayDb *_holidays = nullptr;
  ScheduleEngine *_schedule = nullptr;
  OtaUpdater *_ota = nullptr;
  StatusIndicator *_indicator = nullptr;
  HistoryLog *_history = nullptr;

  void setupRoutes();
  void sendJson(int code, const String &json);
};
