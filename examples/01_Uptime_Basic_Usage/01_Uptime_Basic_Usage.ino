/**
 * UptimeDateTimeProvider — basic usage
 * ------------------------------------
 * Demonstrates:
 *  - prov.begin() — start with base 2000-01-01 00:00:00.000
 *  - prov.nowUtc() — read current time (date + milliseconds from millis())
 *  - prov.adjust(t, AlignMode::ZeroMillis) — snap subsecond to ...SS.000
 *  - prov.adjust(t, AlignMode::PreserveMillis) — keep provided subsecond phase
 *
 * This is a learning/demo sketch. There is no RTC or NTP here — time is derived from millis().
 */

 #include <Arduino.h>
 #include "IDateTimeProvider.h"
 #include "UptimeDateTimeProvider.h"
 
 using namespace sunlix;
 
 UptimeDateTimeProvider prov;
 
 static void printDateTime(const DateTime& t) {
   char buf[64];
   snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
            t.year, t.month, t.day, t.hour, t.minute, t.second, t.millis);
   Serial.println(buf);
 }
 
 void setup() {
   Serial.begin(115200);
   while (!Serial) {}
 
   Serial.println("=== UptimeDateTimeProvider basic ===");
 
   // 1) Initialize provider (base = 2000-01-01 00:00:00.000)
   prov.begin();
 
   // 2) Set an arbitrary time with ZeroMillis (subsecond snapped to .000)
   DateTime t1{2025, 9, 6, 12, 34, 56, 123};   // incoming .123 will be ignored
   prov.adjust(t1, AlignMode::ZeroMillis);      // start at ...56.000
 
   Serial.println("After adjust(.., ZeroMillis):");
   DateTime now{};
   if (prov.nowUtc(now)) printDateTime(now);
 }
 
 void loop() {
   // Print current time every 1 second
   static uint32_t last = 0;
   uint32_t ms = millis();
   if ((uint32_t)(ms - last) >= 1000) {
     last += 1000;
     DateTime now{};
     if (prov.nowUtc(now)) printDateTime(now);
   }
 }
 