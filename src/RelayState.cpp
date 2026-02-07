#include "RelayState.h"

#include <LittleFS.h>

namespace {
constexpr const char *kRelayStatePath = "/relay_state.txt";
} // namespace

bool relaystate::load(bool &outRelayOn) {
  outRelayOn = false;
  if (!LittleFS.exists(kRelayStatePath)) return false;
  File file = LittleFS.open(kRelayStatePath, "r");
  if (!file) return false;
  const int c = file.read();
  file.close();

  if (c == '1') {
    outRelayOn = true;
    return true;
  }
  if (c == '0') {
    outRelayOn = false;
    return true;
  }
  return false;
}

bool relaystate::save(bool relayOn) {
  File file = LittleFS.open(kRelayStatePath, "w");
  if (!file) return false;
  const size_t written = file.print(relayOn ? "1\n" : "0\n");
  file.close();
  return written > 0;
}

