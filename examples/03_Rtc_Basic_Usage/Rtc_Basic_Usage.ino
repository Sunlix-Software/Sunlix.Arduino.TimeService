/**
 * Example: RtcBasicUsage
 * ----------------------
 * DS3231 + SQW(1 Hz) timestamp via RtcDateTimeProvider.
 *
 * What it does:
 *  - Initializes DS3231 and enables SQW 1 Hz (optional).
 *  - Blocks in begin() until the *next* SQW edge (up to timeout), binding:
 *      baseUnix   (RTC second on that real edge)
 *      baseEdgeUs (micros() captured by ISR on that edge)
 *  - Prints current UTC time every 250 ms as YYYY-MM-DD HH:MM:SS.mmm
 *
 * Wiring (typical DS3231 breakout):
 *  - DS3231 SDA  -> MCU SDA
 *  - DS3231 SCL  -> MCU SCL
 *  - DS3231 SQW  -> MCU pin 2 (interrupt-capable; adjust below if needed)
 *  - VCC/GND as usual
 *
 * Notes:
 *  - No I2C inside ISR (only micros()).
 *  - If the RTC lost power, you can adjust it to the build time (see commented section).
 *  - If begin() times out and requireBind==true, sketch will stop with an error message.
 */

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

#include "IDateTimeProvider.h"
#include "RtcDateTimeProvider.h"

// ====== User config ======
static constexpr uint8_t   SQW_PIN        = 2;        // SQW -> D2 (change if needed)
static constexpr PinStatus SQW_EDGE       = RISING;   // RISING or FALLING (choose one)
static constexpr bool      ENABLE_SQW_1HZ = true;     // program DS3231 SQW=1Hz on begin
static constexpr uint16_t  BIND_TIMEOUT   = 1500;     // ms to wait for the next edge (0=infinite)
static constexpr bool      REQUIRE_BIND   = true;     // fail begin() if no edge within timeout
static constexpr uint32_t  PRINT_PERIOD   = 250;      // ms

RTC_DS3231 rtc;
sunlix::RtcDateTimeProvider* prov = nullptr;

static void printDateTime(const sunlix::DateTime& t) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
           t.year, t.month, t.day, t.hour, t.minute, t.second, t.millis);
  Serial.println(buf);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { /* wait for USB CDC if needed */ }
  Serial.println(F("=== RtcBasicUsage (DS3231 SQW) ==="));

  Wire.begin();

  // Bring up the RTC device first.
  if (!rtc.begin()) {
    Serial.println(F("ERROR: DS3231 not responding (rtc.begin() failed)"));
    while (1) { delay(1000); }
  }

  // Optional: set RTC if it lost power (adjust to build time; UTC assumed).
  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power — adjusting to build time (UTC assumed)."));
    rtc.adjust(::DateTime(F(__DATE__), F(__TIME__)));  // RTClib's DateTime
  }

  // Configure the provider (SQW-based).
  sunlix::RtcDateTimeProvider::Config cfg;
  cfg.rtc           = &rtc;
  cfg.sqwPin        = SQW_PIN;
  cfg.sqwEdge       = SQW_EDGE;        // PinStatus
  cfg.enableSqw1Hz  = ENABLE_SQW_1HZ;
  cfg.bindTimeoutMs = BIND_TIMEOUT;
  cfg.requireBind   = REQUIRE_BIND;

  static sunlix::RtcDateTimeProvider provider(cfg); // static to keep storage
  prov = &provider;

  // Begin: wait for the next SQW edge up to BIND_TIMEOUT.
  if (!prov->begin()) {
    Serial.println(F("ERROR: RtcDateTimeProvider.begin() failed (no SQW edge?)"));
    while (1) { delay(1000); }
  }

  Serial.println(F("Provider started."));
  if (prov->status() == sunlix::TimeStatus::LostPower) {
    Serial.println(F("NOTE: RTC had lost power earlier; time was adjusted or should be."));
  }
}

void loop() {
  static uint32_t lastPrint = 0;
  const uint32_t nowMs = millis();

  if ((uint32_t)(nowMs - lastPrint) >= PRINT_PERIOD) {
    lastPrint = nowMs;

    if (!prov) return;

    sunlix::DateTime t{};
    if (prov->nowUtc(t)) {
      printDateTime(t);
    } else {
      // If you ever get here, provider likely has NoDevice/NotStarted status.
      Serial.println(F("NO TIME"));
    }
  }
}