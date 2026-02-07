#include "HistoryLog.h"

#include <LittleFS.h>

namespace {
constexpr const char *kHistoryPath = "/history.log";
constexpr uint32_t kMaxFileBytes = 12UL * 1024UL;

String sanitize(const String &msg) {
  String out = msg;
  out.replace("\n", " ");
  out.replace("\r", " ");
  out.replace("|", " ");
  out.trim();
  return out;
}

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i += 1) {
    const char c = s[i];
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}
} // namespace

const char *HistoryLog::kindToString(HistoryKind kind) {
  switch (kind) {
  case HistoryKind::Boot:
    return "boot";
  case HistoryKind::Relay:
    return "relay";
  case HistoryKind::Network:
    return "network";
  case HistoryKind::Clock:
    return "clock";
  case HistoryKind::Update:
    return "update";
  default:
    return "other";
  }
}

void HistoryLog::resetMemory() {
  _count = 0;
  _next = 0;
  for (uint8_t i = 0; i < kMaxEntries; i += 1) {
    _entries[i] = {};
  }
}

void HistoryLog::push(const Entry &e) {
  _entries[_next] = e;
  _next = static_cast<uint8_t>((_next + 1) % kMaxEntries);
  if (_count < kMaxEntries) _count += 1;
}

bool HistoryLog::getLogical(uint16_t logicalIndex, Entry &out) const {
  if (logicalIndex >= _count) return false;
  const uint8_t oldest = (_count == kMaxEntries) ? _next : 0;
  const uint8_t idx = static_cast<uint8_t>((oldest + logicalIndex) % kMaxEntries);
  out = _entries[idx];
  return true;
}

bool HistoryLog::parseLine(const String &line, Entry &out) {
  String s = line;
  s.trim();
  if (!s.length()) return false;
  const int p1 = s.indexOf('|');
  if (p1 < 0) return false;
  const int p2 = s.indexOf('|', p1 + 1);
  if (p2 < 0) return false;

  const uint32_t t = static_cast<uint32_t>(s.substring(0, p1).toInt());
  const String k = s.substring(p1 + 1, p2);
  const String msg = s.substring(p2 + 1);

  HistoryKind kind = HistoryKind::Boot;
  if (k == "relay") kind = HistoryKind::Relay;
  else if (k == "network") kind = HistoryKind::Network;
  else if (k == "clock") kind = HistoryKind::Clock;
  else if (k == "update") kind = HistoryKind::Update;
  else if (k == "boot") kind = HistoryKind::Boot;

  out = {};
  out.localEpoch = t;
  out.kind = kind;
  const String clean = sanitize(msg);
  strncpy(out.msg, clean.c_str(), sizeof(out.msg) - 1);
  out.msg[sizeof(out.msg) - 1] = '\0';
  return true;
}

bool HistoryLog::appendToFile(const Entry &e) const {
  File f = LittleFS.open(kHistoryPath, "a");
  if (!f) return false;
  f.print(String(e.localEpoch));
  f.print("|");
  f.print(kindToString(e.kind));
  f.print("|");
  f.print(e.msg);
  f.print("\n");
  f.close();
  return true;
}

bool HistoryLog::compactFile() const {
  const String tmp = String(kHistoryPath) + ".tmp";
  File out = LittleFS.open(tmp, "w");
  if (!out) return false;

  Entry e{};
  for (uint16_t i = 0; i < _count; i += 1) {
    if (!getLogical(i, e)) continue;
    out.print(String(e.localEpoch));
    out.print("|");
    out.print(kindToString(e.kind));
    out.print("|");
    out.print(e.msg);
    out.print("\n");
  }
  out.close();

  LittleFS.remove(kHistoryPath);
  if (!LittleFS.rename(tmp, kHistoryPath)) {
    LittleFS.remove(tmp);
    return false;
  }
  return true;
}

void HistoryLog::maybeCompactFile() const {
  File f = LittleFS.open(kHistoryPath, "r");
  if (!f) return;
  const size_t size = f.size();
  f.close();
  if (size > kMaxFileBytes) {
    compactFile();
  }
}

String HistoryLog::serializeEntryJson(const Entry &e) {
  String msg = String(e.msg);
  const String esc = jsonEscape(msg);
  String out;
  out.reserve(64 + esc.length());
  out += "{\"t\":";
  out += String(e.localEpoch);
  out += ",\"kind\":\"";
  out += kindToString(e.kind);
  out += "\",\"msg\":\"";
  out += esc;
  out += "\"}";
  return out;
}

void HistoryLog::begin() {
  resetMemory();

  if (!LittleFS.exists(kHistoryPath)) return;
  File f = LittleFS.open(kHistoryPath, "r");
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    Entry e{};
    if (parseLine(line, e)) push(e);
    yield();
  }
  f.close();

  // Keep file small and consistent with in-memory ring.
  compactFile();
}

void HistoryLog::clear() {
  resetMemory();
  LittleFS.remove(kHistoryPath);
}

void HistoryLog::add(uint32_t localEpoch, HistoryKind kind, const String &message) {
  Entry e{};
  e.localEpoch = localEpoch;
  e.kind = kind;
  const String clean = sanitize(message);
  strncpy(e.msg, clean.c_str(), sizeof(e.msg) - 1);
  e.msg[sizeof(e.msg) - 1] = '\0';

  push(e);
  appendToFile(e);
  maybeCompactFile();
}

String HistoryLog::toJson(uint16_t limit) const {
  if (limit == 0) limit = 1;
  if (limit > _count) limit = _count;

  String out;
  out.reserve(2048);
  out += "{\"ok\":true,\"items\":[";

  const uint16_t start = (_count > limit) ? static_cast<uint16_t>(_count - limit) : 0;
  bool first = true;
  for (uint16_t i = start; i < _count; i += 1) {
    Entry e{};
    if (!getLogical(i, e)) continue;
    if (!first) out += ",";
    first = false;
    out += serializeEntryJson(e);
  }
  out += "]}";
  return out;
}
