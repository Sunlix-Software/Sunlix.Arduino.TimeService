#pragma once
#include <cstdint>

/**
 * @file IDateTimeProvider.h
 * @brief Lightweight time provider interface for MCUs.
 *
 * Notes:
 *  - No dynamic allocation; fast calls.
 *  - Subsecond precision via `millis` (0..999); 0 means “not provided”.
 */

namespace sunlix {

  /// Simple timestamp container (date + time + optional milliseconds).
  struct DateTime {
    std::uint16_t year;    ///< e.g., 2025
    std::uint8_t  month;   ///< 1..12
    std::uint8_t  day;     ///< 1..31
    std::uint8_t  hour;    ///< 0..23
    std::uint8_t  minute;  ///< 0..59
    std::uint8_t  second;  ///< 0..59
    std::uint16_t millis;  ///< 0..999; 0 = not provided
  };

  /// Provider health.
  enum class TimeStatus : std::uint8_t {
    Ok,
    NotStarted,
    LostPower,
    NoDevice
  };

  /// Policy for handling subsecond phase when applying a new time.
  enum class AlignMode : std::uint8_t {
    /**
     * Keep provided `utc.millis` as phase anchor.
     * Use when the upstream source provides trustworthy subsecond timing
     * (e.g., local NTP on LAN, GPS/PPS).
     */
    PreserveMillis,

    /**
     * Snap immediately to ...SS.000 and derive subsequent milliseconds
     * from MCU monotonic timer (e.g., millis()).
     * Recommended for WAN NTP where subsecond accuracy is unreliable.
     */
    ZeroMillis,

    /**
     * Wait for the next second boundary and then snap to .000.
     * Useful to align strictly to hardware RTC second edges.
     * Implementations should cap the wait (e.g., ~1.2 s).
     */
    AlignToSecond
  };

  /// Abstract time provider (e.g., RTC-backed or uptime-backed).
  struct IDateTimeProvider {
    virtual ~IDateTimeProvider() = default;

    /// Initialize underlying resources/hardware (idempotent).
    virtual bool begin() = 0;

    /**
     * Get current time in UTC.
     * @param[out] outUtc Filled on success; fields normalized; millis in [0..999].
     * @return true if time is available.
     */
    virtual bool nowUtc(DateTime& outUtc) = 0;

    /**
     * Apply a new time value.
     * @param[in] t     New time (millis expected in [0..999]; out-of-range treated as 0).
     * @param[in] mode  Subsecond alignment policy (default: ZeroMillis).
     * @return true if applied.
     */
    virtual bool adjust(const DateTime& t, AlignMode mode = AlignMode::ZeroMillis) = 0;

    /// Current provider status.
    virtual TimeStatus status() const = 0;
  };
}
