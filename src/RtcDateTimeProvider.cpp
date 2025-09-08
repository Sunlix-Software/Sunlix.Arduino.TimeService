#include "RtcDateTimeProvider.h"

namespace sunlix {

RtcDateTimeProvider* RtcDateTimeProvider::s_active_ = nullptr;

RtcDateTimeProvider::RtcDateTimeProvider(const Config& cfg)
: cfg_(cfg) {}

// --- ISR ---

void RtcDateTimeProvider::isrThunk_() {
  if (s_active_) s_active_->onEdgeIsr_();
}

void RtcDateTimeProvider::onEdgeIsr_() {
  const uint32_t nowUs = micros();
  lastIsrUs_ = nowUs;
  edgeSeq_++;

  if (!bound_) return;

  // How many full seconds elapsed since the last bound edge?
  const uint32_t d_us = nowUs - baseEdgeUs_;   // wrap-safe (unsigned)
  uint32_t n = d_us / 1'000'000UL;             // usually 1; >1 if stalled/blocked
  if (n == 0) n = 1;                           // at least one second definitely passed

  baseUnix_   += n;
  // Anchor to the *actual* measured edge (reduces drift from ISR latency variance).
  baseEdgeUs_  = nowUs;
}

// --- Helpers ---

void RtcDateTimeProvider::mapRtclibToApp(const ::DateTime& in, DateTime& out) {
  out.year   = static_cast<std::uint16_t>(in.year());
  out.month  = static_cast<std::uint8_t >(in.month());
  out.day    = static_cast<std::uint8_t >(in.day());
  out.hour   = static_cast<std::uint8_t >(in.hour());
  out.minute = static_cast<std::uint8_t >(in.minute());
  out.second = static_cast<std::uint8_t >(in.second());
  out.millis = 0;
}

::DateTime RtcDateTimeProvider::rtclibFromApp(const DateTime& in) {
  return ::DateTime(in.year, in.month, in.day, in.hour, in.minute, in.second);
}

// Wait for the next SQW edge and bind baseUnix_/baseEdgeUs_ to that edge.
bool RtcDateTimeProvider::bindOnNextEdge_(uint16_t timeoutMs) {
  // Snapshot current edge counter
  noInterrupts();
  const uint32_t seq0 = edgeSeq_;
  interrupts();

  const uint32_t startMs = millis();
  while (true) {
    // Has an edge arrived?
    noInterrupts();
    const uint32_t seqNow = edgeSeq_;
    const uint32_t edgeUs = lastIsrUs_;
    interrupts();

    if (seqNow != seq0) {
      // Bind base to this real edge
      if (!cfg_.rtc) { status_ = TimeStatus::NoDevice; return false; }
      ::DateTime dt = cfg_.rtc->now(); // seconds *after* the edge
      noInterrupts();
      baseUnix_   = dt.unixtime();
      baseEdgeUs_ = edgeUs;
      bound_      = true;
      interrupts();
      status_ = TimeStatus::Ok;
      return true;
    }

    if (timeoutMs && static_cast<uint32_t>(millis() - startMs) >= timeoutMs) {
      return false;
    }
    delay(1); // be polite to the scheduler
  }
}

// --- IDateTimeProvider ---

bool RtcDateTimeProvider::begin() {
  if (!cfg_.rtc) { status_ = TimeStatus::NoDevice; return false; }

  // (Optional) probe device responsiveness early
  if (!cfg_.rtc->begin()) { status_ = TimeStatus::NoDevice; return false; }

  s_active_ = this; // install ISR target

  if (cfg_.enableSqw1Hz) {
    cfg_.rtc->writeSqwPinMode(DS3231_SquareWave1Hz);
  }

  pinMode(cfg_.sqwPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(cfg_.sqwPin), &RtcDateTimeProvider::isrThunk_, cfg_.sqwEdge);

  // Clear base
  noInterrupts();
  bound_      = false;
  baseUnix_   = 0;
  baseEdgeUs_ = 0;
  edgeSeq_    = 0;
  interrupts();

  // Strict bind to the *next* real edge (per config)
  if (!bindOnNextEdge_(cfg_.bindTimeoutMs)) {
    if (cfg_.requireBind) { status_ = TimeStatus::NoDevice; return false; }
    // Soft start: not bound yet; nowUtc() will return seconds with .000 until first edge arrives.
    status_ = cfg_.rtc->lostPower() ? TimeStatus::LostPower : TimeStatus::Ok;
  } else {
    status_ = cfg_.rtc->lostPower() ? TimeStatus::LostPower : TimeStatus::Ok;
  }
  return true;
}

bool RtcDateTimeProvider::nowUtc(DateTime& out) {
  if (!cfg_.rtc) { status_ = TimeStatus::NoDevice; return false; }

  // If not bound yet (soft mode), we cannot produce subsecond → seconds-only fallback.
  noInterrupts();
  const bool     bound    = bound_;
  const uint32_t baseUnix = baseUnix_;
  const uint32_t baseEdge = baseEdgeUs_;
  interrupts();

  if (!bound) {
    // One I2C read for seconds-only truth
    ::DateTime now = cfg_.rtc->now();
    mapRtclibToApp(now, out);
    out.millis = 0;                 // subsecond not provided
    // Keep status: Ok or LostPower depending on last known flag
    status_ = cfg_.rtc->lostPower() ? TimeStatus::LostPower : TimeStatus::Ok;
    return true;
  }

  // Bound path: zero I2C here
  const uint32_t nowUs = micros();
  const uint32_t d_us  = nowUs - baseEdge;            // wrap-safe
  const uint32_t whole = d_us / 1'000'000UL;
  const uint32_t remus = d_us - whole * 1'000'000UL;

  const uint32_t unixNow = baseUnix + whole;
  ::DateTime dt(unixNow);

  mapRtclibToApp(dt, out);
  out.millis = static_cast<std::uint16_t>(remus / 1000UL); // 0..999

  // Keep Ok even if RTC once reported LostPower; that flag is sticky until adjust()
  if (status_ == TimeStatus::NotStarted) status_ = TimeStatus::Ok;
  return true;
}

bool RtcDateTimeProvider::adjust(const DateTime& t) {
  if (!cfg_.rtc) { status_ = TimeStatus::NoDevice; return false; }

  // 1) Write new time to RTC (seconds only; millis are undefined on DS3231)
  ::DateTime rt = rtclibFromApp(t);
  cfg_.rtc->adjust(rt);

  // 2) Re-bind base at the next real edge (up to bindTimeoutMs)
  noInterrupts(); bound_ = false; interrupts();
  if (!bindOnNextEdge_(cfg_.bindTimeoutMs)) {
    if (cfg_.requireBind) { status_ = TimeStatus::NoDevice; return false; }
    // Soft: stay unbound; nowUtc() will return seconds + .000 until edge arrives.
  }
  status_ = TimeStatus::Ok;
  return true;
}

bool RtcDateTimeProvider::isBound() const {
  noInterrupts(); bool b = bound_; interrupts(); return b;
}

}