#include "core/clock.h"

#include <math.h>

extern "C" {
#include <libavutil/time.h>
}

namespace core {

Clock::Clock(int* queue_serial) : paused_(false), speed_(1.0), queue_serial_(queue_serial) {
  SetClock(NAN, -1);
}

void Clock::SyncClockToSlave(Clock* c, Clock* slave, double no_sync_threshold) {
  double clock = c->GetClock();
  double slave_clock = slave->GetClock();
  if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > no_sync_threshold)) {
    c->SetClock(slave_clock, slave->serial_);
  }
}

void Clock::SetClockSpeed(double speed) {
  SetClock(GetClock(), serial_);
  speed_ = speed;
}

void Clock::SetClockAt(double pts, int serial, double time) {
  pts_ = pts;
  last_updated_ = time;
  pts_drift_ = pts - time;
  serial_ = serial;
}

void Clock::SetClock(double pts, int serial) {
  double time = av_gettime_relative() / 1000000.0;
  SetClockAt(pts, serial, time);
}

double Clock::GetClock() const {
  if (*queue_serial_ != serial_) {
    return NAN;
  }
  if (paused_) {
    return pts_;
  }

  double time = av_gettime_relative() / 1000000.0;
  return pts_drift_ + time - (time - last_updated_) * (1.0 - speed_);
}

double Clock::Speed() const {
  return speed_;
}

int Clock::Serial() const {
  return serial_;
}

double Clock::LastUpdated() const {
  return last_updated_;
}

double Clock::Pts() const {
  return pts_;
}

void Clock::SetPaused(bool paused) {
  paused_ = paused;
}

}  // namespace core
