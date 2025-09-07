/**
 * Demo: UptimeDateTimeProvider alignment modes
 * --------------------------------------------
 * ⚠️ NOT FOR PRODUCTION. This sketch is a didactic demo to visualize how
 *    UptimeDateTimeProvider handles subsecond alignment:
 *      - ZeroMillis      : start at ...SS.000 and stitch millis from MCU timer
 *      - PreserveMillis  : keep provided subsecond phase (e.g., .420)
 *      - AlignToSecond   : for uptime-only behaves like ZeroMillis (no RTC edge)
 *
 * What this demo does:
 *  - Switches mode every MODE_DURATION_MS and prints the current timestamp
 *    every PRINT_PERIOD_MS.
 *  - Immediately after each adjust() it prints one sample to show the exact
 *    starting phase (.000 or the preserved .xxx), and then continues periodic
 *    printing synchronized from that moment.
 *
 * Limitations:
 *  - Serial printing and loop scheduling introduce jitter; times may drift by a few ms.
 *  - No RTC, no NTP here — ONLY uptime-based time (millis()).
 *  - This pattern is for understanding behavior, not a logging/reference design.
 */

 #include <Arduino.h>
 #include "IDateTimeProvider.h"
 #include "UptimeDateTimeProvider.h"
 
 using namespace sunlix;
 
 // ---------------- Demo settings ----------------
 static const DateTime DEMO_BASE = {
   2025,  // year
   9,     // month
   6,     // day
   5,     // hour
   10,    // minute
   23,    // second
   420    // millis (we'll demonstrate preserving this phase)
 };
 
 static const uint32_t PRINT_PERIOD_MS = 250;   // how often we print current time
 static const uint32_t MODE_DURATION_MS = 6000; // how long to show each mode
 
 // ---------------- Globals ----------------
 UptimeDateTimeProvider prov;
 
 enum class Mode : uint8_t { Zero, Preserve, AlignToSecond };
 Mode current = Mode::Zero;
 
 uint32_t lastPrintMs = 0;
 uint32_t modeStartMs = 0;
 
 // ---------------- Helpers ----------------
 static const char* modeName(Mode m) {
   switch (m) {
     case Mode::Zero:           return "ZeroMillis";
     case Mode::Preserve:       return "PreserveMillis";
     case Mode::AlignToSecond:  return "AlignToSecond";
     default:                   return "?";
   }
 }
 
 static void printDateTime(const DateTime& t) {
   char buf[64];
   snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
            t.year, t.month, t.day, t.hour, t.minute, t.second, t.millis);
   Serial.println(buf);
 }
 
 static void applyMode(Mode m) {
   Serial.println();
   Serial.print(">>> Switching mode to: ");
   Serial.println(modeName(m));
 
   switch (m) {
     case Mode::Zero:
       prov.adjust(DEMO_BASE, AlignMode::ZeroMillis);
       break;
     case Mode::Preserve:
       prov.adjust(DEMO_BASE, AlignMode::PreserveMillis);
       break;
     case Mode::AlignToSecond:
       prov.adjust(DEMO_BASE, AlignMode::AlignToSecond);
       break;
   }
 
   modeStartMs = millis();
   lastPrintMs = modeStartMs;
 
   DateTime t{};
   if (prov.nowUtc(t)) {
     Serial.print(modeName(m));
     Serial.print("  ");
     printDateTime(t);
   }
 }
 
 // ---------------- Arduino ----------------
 void setup() {
   Serial.begin(115200);
   while (!Serial) {}
 
   Serial.println("=== UptimeDateTimeProvider modes demo ===");
   Serial.println("Base time: 2025-09-06 05:10:23.420");
   Serial.println("Modes rotate every 6 seconds: ZeroMillis -> PreserveMillis -> AlignToSecond -> repeat");
   Serial.println();
 
   prov.begin();
   applyMode(current);
   lastPrintMs = millis();
 }
 
 void loop() {
   // Periodic print
   uint32_t now = millis();
   if ((uint32_t)(now - lastPrintMs) >= PRINT_PERIOD_MS) {
     lastPrintMs = now;
 
     DateTime t{};
     if (prov.nowUtc(t)) {
       Serial.print(modeName(current));
       Serial.print("  ");
       printDateTime(t);
     } else {
       Serial.println("NO TIME");
     }
   }
 
   // Rotate mode after MODE_DURATION_MS
   if ((uint32_t)(now - modeStartMs) >= MODE_DURATION_MS) {
     // next mode
     if (current == Mode::Zero) current = Mode::Preserve;
     else if (current == Mode::Preserve) current = Mode::AlignToSecond;
     else current = Mode::Zero;
 
     applyMode(current);
   }
 }