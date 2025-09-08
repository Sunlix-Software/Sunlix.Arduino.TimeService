/**
 * Example: TimeService_NTP_WiFi (UNO R4 WiFi)
 * -------------------------------------------
 * - Connects to Wi-Fi (WiFiS3)
 * - Fetches UTC from NTP over UDP (pool.ntp.org)
 * - Feeds TimeService via callback (ntpFetchUtc)
 * - Uses RTC DS3231 + SQW if present for sub-second phase; otherwise Uptime
 *
 * Prints current time every 500 ms as: YYYY-MM-DD HH:MM:SS.mmm
 *
 * Wiring for DS3231 (optional but recommended for sub-second accuracy):
 *   - DS3231 SDA  -> MCU SDA
 *   - DS3231 SCL  -> MCU SCL
 *   - DS3231 SQW  -> MCU pin 2 (interrupt-capable)
 *   - VCC / GND
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <RTClib.h>

#include "IDateTimeProvider.h"
#include "UptimeDateTimeProvider.h"
#include "RtcDateTimeProvider.h"
#include "TimeService.h"

using namespace sunlix;

// ====================== User settings ======================
#define WIFI_SSID     "senza"
#define WIFI_PASSWORD "12345678"

static constexpr const char* NTP_HOST     = "pool.ntp.org";
static constexpr uint16_t    NTP_LOCAL_PORT = 2390;
static constexpr uint16_t    NTP_TIMEOUT_MS = 1200;
static constexpr uint8_t     NTP_RETRIES    = 2;   // total attempts = 1 + retries

// RTC (optional)
static constexpr uint8_t   SQW_PIN        = 2;
static constexpr PinStatus SQW_EDGE       = RISING;
static constexpr bool      ENABLE_SQW_1HZ = true;
static constexpr uint16_t  BIND_TIMEOUT   = 1500;
static constexpr bool      REQUIRE_BIND   = true;

// Print cadence
static constexpr uint32_t  PRINT_PERIOD_MS = 500;

// ====================== Globals ======================
RTC_DS3231 rtc;
TimeService* ts = nullptr;
WiFiUDP udp;

// ====================== Helpers ======================
static void printDateTime(const sunlix::DateTime& t) {
  char buf[48];
  snprintf(buf, sizeof(buf),
           "%04u-%02u-%02u %02u:%02u:%02u.%03u",
           t.year, t.month, t.day, t.hour, t.minute, t.second, t.millis);
  Serial.println(buf);
}

static const char* wlStatusName(int s) {
  switch (s) {
    case WL_NO_SHIELD:   return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL:return "NO_SSID";
    case WL_SCAN_COMPLETED:return "SCAN_COMPLETED";
    case WL_CONNECTED:   return "CONNECTED";
    case WL_CONNECT_FAILED:return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:return "CONNECTION_LOST";
    case WL_DISCONNECTED:return "DISCONNECTED";
    default:             return "?";
  }
}

// Connect to Wi-Fi with simple diagnostics
static bool connectWiFi(uint32_t overallTimeoutMs = 15000) {
  Serial.print(F("Connecting to WiFi: "));
  Serial.println(F(WIFI_SSID));

  uint32_t start = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (true) {
    int st = WiFi.status();
    if (st == WL_CONNECTED) {
      Serial.print(F("WiFi connected. IP: "));
      Serial.println(WiFi.localIP());
      return true;
    }
    if ((millis() - start) > overallTimeoutMs) {
      Serial.print(F("WiFi connect timeout, status="));
      Serial.println(wlStatusName(st));
      return false;
    }
    Serial.print('.');
    delay(300);
  }
}

// Convert 64-bit NTP (seconds since 1900) → Unix epoch seconds (since 1970)
static bool ntpToUnix(uint32_t secsSince1900, uint32_t& outUnix) {
  // 2208988800 = seconds between 1900-01-01 and 1970-01-01
  const uint32_t seventyYears = 2208988800UL;
  if (secsSince1900 < seventyYears) return false;
  outUnix = secsSince1900 - seventyYears;
  return true;
}

// ====================== NTP fetch callback ======================
static bool fetchNtpUtc(sunlix::DateTime& outUtc) {
  // Ensure Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectWiFi()) return false;
  }

  // Start UDP
  if (!udp.begin(NTP_LOCAL_PORT)) {
    Serial.println(F("UDP.begin() failed"));
    return false;
  }

  uint8_t packetBuffer[48];
  memset(packetBuffer, 0, sizeof(packetBuffer));

  // NTP request header: LI=0, VN=4, Mode=3 (client)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;  // Stratum
  packetBuffer[2] = 6;  // Poll
  packetBuffer[3] = 0xEC; // Precision
  // bytes 12..15 (transmit timestamp "reference ID") can be any, leave 0

  for (uint8_t attempt = 0; attempt <= NTP_RETRIES; ++attempt) {
    // Send packet
    if (udp.beginPacket(NTP_HOST, 123) == 0) {
      Serial.println(F("beginPacket() failed"));
      continue;
    }
    if (udp.write(packetBuffer, sizeof(packetBuffer)) != sizeof(packetBuffer)) {
      Serial.println(F("udp.write() failed"));
      udp.endPacket(); // flush
      continue;
    }
    if (!udp.endPacket()) {
      Serial.println(F("endPacket() failed"));
      continue;
    }

    // Wait for response with timeout
    uint32_t start = millis();
    while ((millis() - start) < NTP_TIMEOUT_MS) {
      int sz = udp.parsePacket();
      if (sz >= 48) {
        uint8_t resp[48];
        udp.read(resp, 48);

        // NTP seconds since 1900 are at bytes 40..43 (big-endian)
        uint32_t secs1900 =
          ((uint32_t)resp[40] << 24) |
          ((uint32_t)resp[41] << 16) |
          ((uint32_t)resp[42] <<  8) |
          ((uint32_t)resp[43] <<  0);

        uint32_t unixSecs;
        if (!ntpToUnix(secs1900, unixSecs)) {
          Serial.println(F("NTP epoch conversion failed"));
          return false;
        }

        // Build fields via RTClib's DateTime (easy calendar math)
        ::DateTime dt((uint32_t)unixSecs); // UTC
        outUtc.year   = (uint16_t)dt.year();
        outUtc.month  = (uint8_t) dt.month();
        outUtc.day    = (uint8_t) dt.day();
        outUtc.hour   = (uint8_t) dt.hour();
        outUtc.minute = (uint8_t) dt.minute();
        outUtc.second = (uint8_t) dt.second();
        outUtc.millis = 0; // NTP subsecond not used here

        udp.stop();
        return true;
      }
      delay(10);
    }

    Serial.println(F("NTP timeout, retrying..."));
  }

  udp.stop();
  return false;
}

// ====================== Arduino ======================
void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("=== TimeService_NTP_WiFi (UNO R4 WiFi) ==="));

  Wire.begin();

  // Try bring up RTC (optional)
  bool haveRtc = rtc.begin();
  if (!haveRtc) {
    Serial.println(F("WARNING: DS3231 not responding; will fall back to Uptime provider."));
  } else if (rtc.lostPower()) {
    Serial.println(F("RTC lost power — adjusting to build time (UTC assumed)."));
    rtc.adjust(::DateTime(F(__DATE__), F(__TIME__)));
  }

  // Wi-Fi connect (best effort; NTP will re-check)
  (void)connectWiFi();

  // Configure TimeService
  TimeService::Config cfg;
  cfg.rtc           = haveRtc ? &rtc : nullptr;
  cfg.sqwPin        = SQW_PIN;
  cfg.sqwEdge       = SQW_EDGE;
  cfg.enableSqw1Hz  = ENABLE_SQW_1HZ;
  cfg.bindTimeoutMs = BIND_TIMEOUT;
  cfg.requireBind   = REQUIRE_BIND;

  cfg.ntpOnBegin  = true;              // try once here
  cfg.ntpFetchUtc = &fetchNtpUtc;      // real NTP callback

  static TimeService service(cfg);
  ts = &service;

  if (!ts->begin()) {
    Serial.println(F("ERROR: TimeService.begin() failed"));
    while (1) { delay(1000); }
  }

  // Report provider and NTP telemetry
  switch (ts->activeProvider()) {
    case TimeService::ActiveProvider::Rtc:    Serial.println(F("Active provider: RTC (SQW)")); break;
    case TimeService::ActiveProvider::Uptime: Serial.println(F("Active provider: Uptime"));    break;
    default:                                   Serial.println(F("Active provider: None"));      break;
  }
  Serial.print(F("NTP ever synced: "));   Serial.println(ts->ntpEverSynced() ? F("yes") : F("no"));
  Serial.print(F("NTP last OK: "));       Serial.println(ts->ntpLastOk() ? F("yes") : F("no"));
  Serial.print(F("NTP last attempt ms: ")); Serial.println(ts->ntpLastAttemptMs());
  Serial.print(F("NTP last success ms: ")); Serial.println(ts->ntpLastSuccessMs());
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastNtpTry = 0;

  const uint32_t nowMs = millis();

  // Print current time
  if ((uint32_t)(nowMs - lastPrint) >= PRINT_PERIOD_MS) {
    lastPrint = nowMs;

    sunlix::DateTime t{};
    if (ts && ts->nowUtc(t)) {
      printDateTime(t);
    } else {
      Serial.println(F("NO TIME"));
    }
  }

  // Optional: try NTP periodically (e.g., every 5 minutes) to keep RTC fresh
  const uint32_t NTP_PERIOD_MS = 300000UL;
  if ((uint32_t)(nowMs - lastNtpTry) >= NTP_PERIOD_MS) {
    lastNtpTry = nowMs;
    if (ts) {
      bool ok = ts->ntpSync();
      Serial.print(F("NTP periodic sync: "));
      Serial.println(ok ? F("OK") : F("FAIL"));
    }
  }
}
