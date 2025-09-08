#pragma once
#include <Arduino.h>
#include <RTClib.h>

#include "IDateTimeProvider.h"
#include "RtcDateTimeProvider.h"
#include "UptimeDateTimeProvider.h"

namespace sunlix {

/**
 * @class TimeService
 * @brief Facade that chooses a single time provider (RTC or Uptime) and delegates calls.
 *
 * Behavior:
 *  - On begin():
 *      1) Try RTC provider if RTC is provided in config.
 *      2) Else fall back to Uptime provider.
 *      3) Optionally run one-shot NTP sync (if callback provided).
 *  - nowUtc()/adjust(): delegated to the active provider.
 *  - ntpSync(): public helper to trigger NTP sync at any time.
 *
 * NTP telemetry you can query:
 *  - ntpEverSynced(): whether there has ever been a successful NTP sync.
 *  - ntpLastOk(): result of the last NTP attempt.
 *  - ntpLastAttemptMs(): millis() of the last attempt (0 if none).
 *  - ntpLastSuccessMs(): millis() of the last success (0 if none).
 */
class TimeService final : public IDateTimeProvider {
public:
  /// User-supplied NTP fetch function: must fill UTC time; return true on success.
  using NtpFetchFn = bool (*)(DateTime& outUtc);

  struct Config {
    // --- RTC (DS3231 SQW) ---
    RTC_DS3231* rtc           = nullptr;     ///< If non-null, RTC provider will be attempted.
    uint8_t     sqwPin        = 2;           ///< Interrupt-capable pin wired to DS3231 SQW.
    PinStatus   sqwEdge       = RISING;      ///< RISING or FALLING.
    bool        enableSqw1Hz  = true;        ///< Program DS3231 to 1 Hz SQW on begin().
    uint16_t    bindTimeoutMs = 1500;        ///< Wait for next SQW edge (0 = infinite).
    bool        requireBind   = true;        ///< If true and timeout → RTC begin() fails.

    // --- NTP (optional, callback-based) ---
    bool        ntpOnBegin    = true;        ///< Try NTP once inside begin() if callback provided.
    NtpFetchFn  ntpFetchUtc   = nullptr;     ///< User-provided NTP function (may be nullptr).
  };

  explicit TimeService(const Config& cfg);

  // IDateTimeProvider
  bool begin() override;
  bool nowUtc(DateTime& out) override;
  bool adjust(const DateTime& t) override;
  TimeStatus status() const override;

  // Extra: trigger NTP sync manually.
  bool ntpSync();

  // Active provider kind.
  enum class ActiveProvider : uint8_t { None, Rtc, Uptime };
  ActiveProvider activeProvider() const { return activeKind_; }

  // NTP telemetry
  bool     ntpEverSynced()   const { return ntpEverSynced_; }
  bool     ntpLastOk()       const { return ntpLastOk_; }
  uint32_t ntpLastAttemptMs()const { return ntpLastAttemptMs_; }
  uint32_t ntpLastSuccessMs()const { return ntpLastSuccessMs_; }

private:
  bool makeRtcProvider_();    // instantiate & begin RTC provider (returns success)
  void makeUptimeProvider_(); // begin uptime provider (always succeeds)

private:
  Config cfg_;

  // Concrete providers (allocated at most once)
  RtcDateTimeProvider*    rtcProv_     = nullptr; // created via new when needed
  UptimeDateTimeProvider  uptimeProv_;            // always available

  // Delegation target
  IDateTimeProvider* active_ = nullptr;
  ActiveProvider     activeKind_ = ActiveProvider::None;

  // NTP state
  bool     ntpEverSynced_    = false;
  bool     ntpLastOk_        = false;
  uint32_t ntpLastAttemptMs_ = 0;
  uint32_t ntpLastSuccessMs_ = 0;
};

}