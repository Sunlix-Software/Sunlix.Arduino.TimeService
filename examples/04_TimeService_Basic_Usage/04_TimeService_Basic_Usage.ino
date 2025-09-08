/**
 * Example: TimeService_BasicUsage
 * -------------------------------
 * Facade that selects ONE provider at begin():
 *   - RtcDateTimeProvider (DS3231 + SQW 1Hz), if RTC is present and binds
 *   - otherwise UptimeDateTimeProvider
 *
 * It optionally performs a one-shot NTP sync via user-supplied callback
 * (kept here as a stub to stay networking-agnostic).
 *
 * Prints current UTC time every 500 ms as: YYYY-MM-DD HH:MM:SS.mmm
 *
 * Wiring (if RTC is used):
 *   - DS3231 SDA  -> MCU SDA
 *   - DS3231 SCL  -> MCU SCL
 *   - DS3231 SQW  -> MCU pin 2 (interrupt-capable; adjust below if needed)
 *   - VCC/GND as usual
 *
 * Notes:
 *   - If RTC lost power, we set it to build time (UTC assumed) once.
 *   - NTP callback is a plain function that fills sunlix::DateTime; return true on success.
 *   - You can query NTP telemetry: ntpEverSynced(), ntpLastOk(), ntpLastAttemptMs(), ntpLastSuccessMs().
 */

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

#include "IDateTimeProvider.h"
#include "RtcDateTimeProvider.h"
#include "UptimeDateTimeProvider.h"
#include "TimeService.h"

using namespace sunlix;

// ===== Demo toggle =====
// Set to 1 to simulate a successful NTP sync to the build time (for demo without networking).
#define SIMULATE_NTP_SUCCESS 0

// ===== User config =====
static constexpr uint8_t   SQW_PIN        = 2;        // SQW -> D2 (change if needed)
static constexpr PinStatus SQW_EDGE       = RISING;   // RISING or FALLING
static constexpr bool      ENABLE_SQW_1HZ = true;     // program DS3231 SQW=1Hz on begin
static constexpr uint16_t  BIND_TIMEOUT   = 1500;     // ms to wait for the next edge (0=infinite)
static constexpr bool      REQUIRE_BIND   = true;     // fail begin() if no edge within timeout
static constexpr uint32_t  PRINT_PERIOD   = 500;      // ms between prints
static constexpr uint32_t  NTP_PERIOD_MS  = 300000;   // try NTP every 5 minutes (demo)

// ===== Globals =====
RTC_DS3231 rtc;
TimeService* ts = nullptr;

// Pretty-print helper
static void printDateTime(const sunlix::DateTime& t) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
           t.year, t.month, t.day, t.hour, t.minute, t.second, t.millis);
  Serial.println(buf);
}

// --- Optional NTP fetch callback (networking-agnostic stub) ---
static bool fetchNtpUtc(sunlix::DateTime& outUtc) {
#if SIMULATE_NTP_SUCCESS
  // Simulate success by using the build time (treated as UTC for demo)
  ::DateTime build(::DateTime(F(__DATE__), F(__TIME__)));
  outUtc.year   = (uint16_t)build.year();
  outUtc.month  = (uint8_t) build.month();
  outUtc.day    = (uint8_t) build.day();
  outUtc.hour   = (uint8_t) build.hour();
  outUtc.minute = (uint8_t) build.minute();
  outUtc.second = (uint8_t) build.second();
  outUtc.millis = 0;
  return true;
#else
  // Real implementation should:
  //  - ensure connectivity,
  //  - query NTP server,
  //  - fill outUtc (UTC) and return true; otherwise return false.
  return false;
#endif
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("=== TimeService_BasicUsage ==="));

  Wire.begin();

  // Try bring up RTC (it's optional; TimeService will fall back to Uptime).
  bool haveRtc = rtc.begin();

  if (!haveRtc) {
    Serial.println(F("WARNING: DS3231 not responding; will fall back to Uptime provider."));
  } else if (rtc.lostPower()) {
    Serial.println(F("RTC lost power — adjusting to build time (UTC assumed)."));
    rtc.adjust(::DateTime(F(__DATE__), F(__TIME__))); // RTClib's DateTime
  }

  // Prepare TimeService config
  TimeService::Config cfg;
  cfg.rtc           = haveRtc ? &rtc : nullptr;
  cfg.sqwPin        = SQW_PIN;
  cfg.sqwEdge       = SQW_EDGE;
  cfg.enableSqw1Hz  = ENABLE_SQW_1HZ;
  cfg.bindTimeoutMs = BIND_TIMEOUT;
  cfg.requireBind   = REQUIRE_BIND;

  // NTP integration (optional)
  cfg.ntpOnBegin  = true;              // try once in begin()
  cfg.ntpFetchUtc = &fetchNtpUtc;      // our stub (returns false unless simulated)

  static TimeService service(cfg);
  ts = &service;

  if (!ts->begin()) {
    Serial.println(F("ERROR: TimeService.begin() failed."));
    while (1) { delay(1000); }
  }

  // Announce selected provider
  switch (ts->activeProvider()) {
    case TimeService::ActiveProvider::Rtc:    Serial.println(F("Active provider: RTC (SQW)")); break;
    case TimeService::ActiveProvider::Uptime: Serial.println(F("Active provider: Uptime"));    break;
    default:                                   Serial.println(F("Active provider: None"));      break;
  }

  // Show initial NTP telemetry
  Serial.print(F("NTP ever synced: "));  Serial.println(ts->ntpEverSynced() ? F("yes") : F("no"));
  Serial.print(F("NTP last OK: "));      Serial.println(ts->ntpLastOk() ? F("yes") : F("no"));
  Serial.print(F("NTP last attempt ms: ")); Serial.println(ts->ntpLastAttemptMs());
  Serial.print(F("NTP last success ms: ")); Serial.println(ts->ntpLastSuccessMs());
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastNtpTry = 0;

  const uint32_t nowMs = millis();

  // Periodic time print
  if ((uint32_t)(nowMs - lastPrint) >= PRINT_PERIOD) {
    lastPrint = nowMs;

    sunlix::DateTime t{};
    if (ts && ts->nowUtc(t)) {
      printDateTime(t);
    } else {
      Serial.println(F("NO TIME"));
    }
  }

  // Periodic NTP attempt (optional demo; harmless if fetch returns false)
  if ((uint32_t)(nowMs - lastNtpTry) >= NTP_PERIOD_MS) {
    lastNtpTry = nowMs;
    if (ts) {
      bool ok = ts->ntpSync();
      Serial.print(F("NTP sync attempt: "));
      Serial.println(ok ? F("OK") : F("FAIL"));
    }
  }
}
