#pragma once

#include <Arduino.h>

namespace datemath {

// Days since 1970-01-01 (civil). Algorithm by Howard Hinnant (public domain).
inline int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline void civilFromDays(int64_t z, int &y, unsigned &m, unsigned &d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - static_cast<int64_t>(era) * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);
}

inline uint32_t dateKeyFromYmd(int y, unsigned m, unsigned d) {
  return static_cast<uint32_t>(y * 10000 + static_cast<int>(m) * 100 + static_cast<int>(d));
}

inline bool ymdFromDateKey(uint32_t dateKey, int &y, unsigned &m, unsigned &d) {
  y = static_cast<int>(dateKey / 10000UL);
  m = static_cast<unsigned>((dateKey / 100UL) % 100UL);
  d = static_cast<unsigned>(dateKey % 100UL);
  return y >= 1970 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

inline uint32_t addDays(uint32_t dateKey, int deltaDays) {
  int y;
  unsigned m, d;
  if (!ymdFromDateKey(dateKey, y, m, d)) return dateKey;
  const int64_t days = daysFromCivil(y, m, d) + deltaDays;
  int y2;
  unsigned m2, d2;
  civilFromDays(days, y2, m2, d2);
  return dateKeyFromYmd(y2, m2, d2);
}

// 0=Sun .. 6=Sat
inline int weekday(uint32_t dateKey) {
  int y;
  unsigned m, d;
  if (!ymdFromDateKey(dateKey, y, m, d)) return 0;
  const int64_t days = daysFromCivil(y, m, d);
  int wd = static_cast<int>((days + 4) % 7); // 1970-01-01 was Thursday (4)
  if (wd < 0) wd += 7;
  return wd;
}

inline int64_t localEpochFromDateKeyMinutes(uint32_t dateKey, uint16_t minutesOfDay) {
  int y;
  unsigned m, d;
  if (!ymdFromDateKey(dateKey, y, m, d)) return 0;
  const int64_t days = daysFromCivil(y, m, d);
  return days * 86400LL + static_cast<int64_t>(minutesOfDay) * 60LL;
}

} // namespace datemath

