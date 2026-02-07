#pragma once

#include <Arduino.h>

enum class HistoryKind : uint8_t {
  Boot = 0,
  Relay = 1,
  Network = 2,
  Clock = 3,
  Update = 4,
};

class HistoryLog {
public:
  void begin();
  void clear();

  void add(uint32_t localEpoch, HistoryKind kind, const String &message);

  // JSON: {ok:true, items:[{t,kind,msg}, ...]}
  String toJson(uint16_t limit = 40) const;

private:
  struct Entry {
    uint32_t localEpoch = 0;
    HistoryKind kind = HistoryKind::Boot;
    char msg[96] = {};
  };

  static constexpr uint8_t kMaxEntries = 80;
  Entry _entries[kMaxEntries];
  uint8_t _count = 0;
  uint8_t _next = 0;

  void resetMemory();
  void push(const Entry &e);
  bool getLogical(uint16_t logicalIndex, Entry &out) const;

  bool appendToFile(const Entry &e) const;
  void maybeCompactFile() const;
  bool compactFile() const;

  static const char *kindToString(HistoryKind kind);
  static bool parseLine(const String &line, Entry &out);
  static String serializeEntryJson(const Entry &e);
};

