#include <Arduino.h>
#include "UptimeDateTimeProvider.h"

namespace sunlix {

UptimeDateTimeProvider::UptimeDateTimeProvider() = default;

bool UptimeDateTimeProvider::begin() {
  // Default base: 2000-01-01 00:00:00.000
  base_.year = 2000; base_.month = 1; base_.day = 1;
  base_.hour = 0;    base_.minute = 0; base_.second = 0;
  base_.millis = 0;

  t0_ms_   = millis();
  started_ = true;
  status_  = TimeStatus::Ok;
  return true;
}

bool UptimeDateTimeProvider::nowUtc(DateTime& out) {
  if (!started_) {
    status_ = TimeStatus::NotStarted;
    return false;
  }

  const std::uint32_t now_ms  = millis();
  const std::uint32_t elapsed = now_ms - t0_ms_;   // wrap-safe
  const std::uint32_t add_s   = elapsed / 1000U;
  const std::uint16_t ms      = static_cast<std::uint16_t>(elapsed % 1000U);

  out = addSeconds(base_, add_s);
  out.millis = ms;
  return true;
}

bool UptimeDateTimeProvider::adjust(const DateTime& t) {
  if (!started_) begin();

  // Clamp millis to [0..999]; ignore mode for uptime (we always anchor to provided ms)
  std::uint16_t m = (t.millis <= 999) ? t.millis : 0;

  base_ = t;
  base_.millis = 0;

  t0_ms_ = millis();
  status_ = TimeStatus::Ok;
  return true;
}

TimeStatus UptimeDateTimeProvider::status() const { return status_; }

// --------- helpers ---------

bool UptimeDateTimeProvider::isLeap(std::uint16_t y) {
  return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

std::uint8_t UptimeDateTimeProvider::daysInMonth(std::uint16_t y, std::uint8_t m) {
  static const std::uint8_t dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2) return dm[1] + (isLeap(y) ? 1 : 0);
  return dm[m - 1];
}

DateTime UptimeDateTimeProvider::addSeconds(const DateTime& in, std::uint32_t add_s) {
  DateTime out = in;

  // time-of-day rollover
  std::uint32_t sod = static_cast<std::uint32_t>(in.hour) * 3600U
                    + static_cast<std::uint32_t>(in.minute) * 60U
                    + static_cast<std::uint32_t>(in.second);
  std::uint32_t total = sod + add_s;

  out.hour   = static_cast<std::uint8_t>((total / 3600U) % 24U);
  out.minute = static_cast<std::uint8_t>((total / 60U) % 60U);
  out.second = static_cast<std::uint8_t>( total % 60U );

  // day rollover
  std::uint32_t daysAdd = total / 86400U;
  while (daysAdd--) {
    std::uint8_t dim = daysInMonth(out.year, out.month);
    if (out.day < dim) {
      ++out.day;
    } else {
      out.day = 1;
      if (out.month < 12) ++out.month;
      else { out.month = 1; ++out.year; }
    }
  }

  // millis is set by caller
  return out;
}

}
