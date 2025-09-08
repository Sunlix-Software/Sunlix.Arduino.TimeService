#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include "IDateTimeProvider.h"

namespace sunlix {

/**
 * @class RtcDateTimeProvider
 * @brief DS3231 + SQW(1 Hz) time provider with subsecond phase from micros().
 *
 * Design:
 *  - begin(): waits for the next SQW edge (configurable timeout) and binds a base:
 *      baseUnix   = rtc.now().unixtime() at that real edge,
 *      baseEdgeUs = micros() timestamp captured by ISR at that edge.
 *  - ISR on each SQW edge: NO I2C; only updates base by whole seconds (handles missed edges)
 *    and stores the latest micros() of the edge.
 *  - nowUtc(): NO I2C when bound; computes unix + millis from (baseUnix, baseEdgeUs).
 *              If not bound yet (soft start), returns rtc.now() with millis=0.
 *  - adjust(): writes RTC time and re-binds base on the next edge.
 *
 * Status semantics:
 *  - Ok          : normal operation (bound to SQW) OR seconds-only fallback (see below).
 *  - NotStarted  : begin() not called or failed.
 *  - LostPower   : RTC reported lost power (sticky until re-adjust or external fix).
 *  - NoDevice    : RTC pointer missing or device not responding.
 */
class RtcDateTimeProvider final : public IDateTimeProvider {
public:
  struct Config {
    RTC_DS3231* rtc = nullptr;    ///< Must be non-null; rtc->begin() must be called by the user.
    uint8_t     sqwPin = 2;       ///< Interrupt-capable pin wired to DS3231 SQW.
    PinStatus         sqwEdge = RISING; ///< RISING or FALLING (choose one; do not use CHANGE).
    bool        enableSqw1Hz = true; ///< Set DS3231_SquareWave1Hz on begin().
    uint16_t    bindTimeoutMs = 1500;///< Max time to wait for the next edge (0 = wait forever).
    bool        requireBind   = true;///< If true and timeout fires → begin() returns false.
  };

  explicit RtcDateTimeProvider(const Config& cfg);

  // IDateTimeProvider
  bool begin() override;
  bool nowUtc(DateTime& out) override;
  bool adjust(const DateTime& t) override;
  TimeStatus status() const override { return status_; }

  /// Whether the provider is currently bound to a real SQW edge.
  bool isBound() const;

private:
  // --- ISR plumbing (single active instance) ---
  static void isrThunk_();   // attachInterrupt target
  void onEdgeIsr_();         // instance handler

  // --- helpers ---
  static void mapRtclibToApp(const ::DateTime& in, DateTime& out);
  static ::DateTime rtclibFromApp(const DateTime& in);

  /// Wait for the next SQW edge and bind baseUnix_/baseEdgeUs_; returns success.
  bool bindOnNextEdge_(uint16_t timeoutMs);

private:
  Config     cfg_;
  TimeStatus status_ = TimeStatus::NotStarted;

  // Base mapping to the last *real* second edge
  volatile bool     bound_      = false;  // base is valid
  volatile uint32_t baseUnix_   = 0;      // UNIX second at the last edge
  volatile uint32_t baseEdgeUs_ = 0;      // micros() timestamp of that edge

  // Diagnostics / ISR snapshot
  volatile uint32_t lastIsrUs_  = 0;      // last edge micros
  volatile uint32_t edgeSeq_    = 0;      // edge counter

  // Single-instance ISR target
  static RtcDateTimeProvider* s_active_;
};

}