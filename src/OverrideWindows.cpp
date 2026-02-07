#include "OverrideWindows.h"

namespace {
bool isValidWindow(const ManualTimeWindow &w) {
  if (w.startUtc == 0 || w.endUtc == 0) return false;
  if (w.endUtc <= w.startUtc) return false;
  // Safety: ignore windows that look corrupt (spanning multiple years).
  if ((w.endUtc - w.startUtc) > (60UL * 60UL * 24UL * 365UL * 3UL)) return false;
  return true;
}
} // namespace

ActiveWindowOverride overridesFindActive(const AppConfig &cfg, uint32_t nowUtc) {
  ActiveWindowOverride best{};
  for (uint8_t i = 0; i < cfg.windowCount; i += 1) {
    const ManualTimeWindow &w = cfg.windows[i];
    if (!isValidWindow(w)) continue;
    if (nowUtc < w.startUtc || nowUtc >= w.endUtc) continue;
    if (!best.active || w.startUtc >= best.startUtc) {
      best.active = true;
      best.stateOn = w.on;
      best.startUtc = w.startUtc;
      best.endUtc = w.endUtc;
    }
  }
  return best;
}

bool overridesApply(const AppConfig &cfg, uint32_t nowUtc, bool baseStateOn, bool &outStateOn, ActiveWindowOverride &outActive) {
  outActive = overridesFindActive(cfg, nowUtc);
  if (outActive.active) {
    outStateOn = outActive.stateOn;
    return true;
  }
  outStateOn = baseStateOn;
  return false;
}

