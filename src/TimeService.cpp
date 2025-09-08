#include "TimeService.h"

namespace sunlix {

TimeService::TimeService(const Config& cfg)
: cfg_(cfg) {}

bool TimeService::makeRtcProvider_() {
  if (!cfg_.rtc) return false;

  if (!rtcProv_) {
    RtcDateTimeProvider::Config rc;
    rc.rtc           = cfg_.rtc;
    rc.sqwPin        = cfg_.sqwPin;
    rc.sqwEdge       = cfg_.sqwEdge;
    rc.enableSqw1Hz  = cfg_.enableSqw1Hz;
    rc.bindTimeoutMs = cfg_.bindTimeoutMs;
    rc.requireBind   = cfg_.requireBind;
    rtcProv_ = new RtcDateTimeProvider(rc);
  }

  if (!rtcProv_->begin()) {
    return false; // leave allocated but inactive
  }

  active_     = rtcProv_;
  activeKind_ = ActiveProvider::Rtc;
  return true;
}

void TimeService::makeUptimeProvider_() {
  (void)uptimeProv_.begin();
  active_     = &uptimeProv_;
  activeKind_ = ActiveProvider::Uptime;
}

bool TimeService::begin() {
  // Choose provider once
  if (!makeRtcProvider_()) {
    makeUptimeProvider_();
  }

  // Optional NTP on begin
  if (cfg_.ntpOnBegin && cfg_.ntpFetchUtc) {
    (void)ntpSync(); // ignore failure; caller can query telemetry
  }

  return (active_ != nullptr);
}

bool TimeService::nowUtc(DateTime& out) {
  if (!active_) return false;
  return active_->nowUtc(out);
}

bool TimeService::adjust(const DateTime& t) {
  if (!active_) return false;
  return active_->adjust(t);
}

TimeStatus TimeService::status() const {
  if (!active_) return TimeStatus::NotStarted;
  return active_->status();
}

bool TimeService::ntpSync() {
  if (!cfg_.ntpFetchUtc || !active_) return false;

  ntpLastAttemptMs_ = millis();

  DateTime ntp{};
  bool ok = cfg_.ntpFetchUtc(ntp);
  ntpLastOk_ = ok;

  if (!ok) {
    return false;
  }

  // Apply to active provider (RTC provider will also write seconds to DS3231 and re-bind)
  if (!active_->adjust(ntp)) {
    ntpLastOk_ = false;
    return false;
  }

  ntpEverSynced_  = true;
  ntpLastSuccessMs_ = ntpLastAttemptMs_;
  return true;
}

}