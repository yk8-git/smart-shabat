#pragma once

namespace relaystate {
// Loads the last persisted relay state from LittleFS.
// Returns true if a valid value was loaded.
bool load(bool &outRelayOn);

// Persists relay state to LittleFS. Returns true on success.
bool save(bool relayOn);
} // namespace relaystate

