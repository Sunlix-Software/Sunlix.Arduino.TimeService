#pragma once
#include <cstdint>
#include "IDateTimeProvider.h"

namespace sunlix {

/**
 * @class UptimeDateTimeProvider
 * @brief Time provider based on MCU uptime (millis) with a configurable base.
 *
 * - begin(): sets base to 2000-01-01 00:00:00.000
 * - adjust(): sets a new base and re-anchors milliseconds
 * - nowUtc(): base + (millis() - anchor), with millis in [0..999]
 */
class UptimeDateTimeProvider final : public IDateTimeProvider {
public:
  UptimeDateTimeProvider();

  bool begin() override;
  bool nowUtc(DateTime& out) override;
  bool adjust(const DateTime& t) override;
  TimeStatus status() const override;

private:
  static bool         isLeap(std::uint16_t y);
  static std::uint8_t daysInMonth(std::uint16_t y, std::uint8_t m);
  static DateTime     addSeconds(const DateTime& in, std::uint32_t add_s);

private:
  bool       started_ = false;
  TimeStatus status_  = TimeStatus::NotStarted;

  DateTime   base_{};   // anchored date-time, millis field kept at 0
  std::uint32_t t0_ms_ = 0; // millis() at the base anchor
};

}
