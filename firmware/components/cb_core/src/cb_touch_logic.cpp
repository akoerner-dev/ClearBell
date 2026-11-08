// cb_touch_logic.cpp - see cb_touch_logic.h.
#include "cb_touch_logic.h"

namespace cb {

// ---- TouchChannel ------------------------------------------------------------------------------------------

void TouchChannel::relearn(uint32_t now_ms) {
  ready_ = false;
  pressed_ = false;
  raised_ = false;
  settling_ = true;
  settle_start_ = now_ms;
  settle_n_ = 0;
  settle_sum_ = 0;
  offset_n_ = 0;
  relearns_++;
}

TouchChannel::Event TouchChannel::update(uint32_t raw, uint32_t now_ms) {
  raw_ = raw;
  uint32_t dt = ready_ ? now_ms - last_ms_ : 0;
  last_ms_ = now_ms;

  if (settling_) {
    // Learn the baseline only from a calm period: if the spread grows beyond kSettleSpread (someone touches,
    // the board is being handled), start the period again.
    if (settle_n_ == 0) {
      settle_start_ = now_ms;
      settle_min_ = settle_max_ = raw;
    }
    settle_min_ = raw < settle_min_ ? raw : settle_min_;
    settle_max_ = raw > settle_max_ ? raw : settle_max_;
    settle_sum_ += raw;
    settle_n_++;
    if (settle_min_ > 0 && 100.0f * (settle_max_ - settle_min_) / settle_min_ > kSettleSpread) {
      settle_n_ = 0;
      settle_sum_ = 0;
      return Event::None;
    }
    if (now_ms - settle_start_ >= kSettleMs) {
      baseline_ = (float)(settle_sum_ / settle_n_);
      settling_ = false;
      ready_ = true;
    }
    return Event::None;
  }

  delta_ = baseline_ > 0 ? 100.0f * ((float)raw - baseline_) / baseline_ : 0;

  // Slow tracking only near the baseline. Exponential average with time constant kTrackTauMs.
  if (delta_ > -kFastDownPercent && delta_ < kTrackBand && !pressed_) {
    float k = (float)dt / (float)kTrackTauMs;
    if (k > 1) k = 1;
    baseline_ += ((float)raw - baseline_) * k;
  } else if (delta_ <= -kFastDownPercent) {
    // Far BELOW the baseline: whatever raised it before (e.g. a relearned wet cloth) is gone. Follow down
    // quickly, otherwise the next real touch would look smaller than it is.
    float k = (float)dt / 1000.0f;
    if (k > 1) k = 1;
    baseline_ += ((float)raw - baseline_) * k;
  }

  // Steady offset between the tracking band and the release threshold (FW-B-06): watch for kOffsetAdoptMs; if the
  // value stays within kSettleSpread the whole time, it is the new rest value. A finger is far above +kOffPercent,
  // a hand or wiping cloth does not stay this steady.
  if (!pressed_ && delta_ >= kTrackBand && delta_ < kOffPercent) {
    if (offset_n_ == 0) {
      offset_start_ = now_ms;
      offset_min_ = offset_max_ = raw;
      offset_sum_ = 0;
    }
    offset_min_ = raw < offset_min_ ? raw : offset_min_;
    offset_max_ = raw > offset_max_ ? raw : offset_max_;
    offset_sum_ += raw;
    offset_n_++;
    if (100.0f * (offset_max_ - offset_min_) / offset_min_ > kSettleSpread) {
      offset_n_ = 0;  // not steady: start watching again with the next sample
    } else if (now_ms - offset_start_ >= kOffsetAdoptMs) {
      baseline_ = (float)(offset_sum_ / offset_n_);
      delta_ = 100.0f * ((float)raw - baseline_) / baseline_;
      offset_n_ = 0;
      adopts_++;
    }
  } else {
    offset_n_ = 0;
  }

  bool above_on = delta_ >= kOnPercent;
  bool below_off = delta_ < kOffPercent;
  if (above_on && !raised_) raised_at_ = now_ms;
  raised_ = pressed_ ? !below_off : above_on;

  Event ev = Event::None;
  if (!pressed_ && raised_ && now_ms - raised_at_ >= kBellMinHoldMs) {
    // Debounced press. The layers above apply their own (longer) minimum hold on top.
    pressed_ = true;
    pressed_at_ = raised_at_;
    ev = Event::Pressed;
  } else if (pressed_ && !raised_) {
    pressed_ = false;
    ev = Event::Released;
  } else if (pressed_ && now_ms - pressed_at_ >= kStuckMs) {
    relearn(now_ms);
    ev = Event::Stuck;
  }
  return ev;
}

// ---- BellKeys (outdoor) ----------------------------------------------------------------------------------

BellKeys::Out BellKeys::update(uint32_t raw_eg, uint32_t raw_og, uint32_t now_ms) {
  TouchChannel::Event e1 = eg_.update(raw_eg, now_ms);
  TouchChannel::Event e2 = og_.update(raw_og, now_ms);
  if (e1 == TouchChannel::Event::Released || e1 == TouchChannel::Event::Stuck) fired_eg_ = false;
  if (e2 == TouchChannel::Event::Released || e2 == TouchChannel::Event::Stuck) fired_og_ = false;

  bool both = (eg_.pressed() || eg_.raised()) && (og_.pressed() || og_.raised());
  // During the guard time of a press, the other key only has to be clearly on its way up (FW-B-07).
  auto guarding = [now_ms](const TouchChannel &c, bool fired) {
    return c.pressed() && !fired && now_ms - c.pressed_since() < kBellMinHoldMs + kBellBothGuardMs;
  };
  if ((guarding(eg_, fired_eg_) && og_.delta_percent() >= kOffPercent) ||
      (guarding(og_, fired_og_) && eg_.delta_percent() >= kOffPercent))
    both = true;
  if (both) {
    // Plausibility: a finger presses one key. Both at once = wetting (or a hand on both) -> never ring.
    if (!fired_eg_ || !fired_og_) {
      fired_eg_ = fired_og_ = true;  // no ring for the rest of this contact either
      rejected_++;
      return Out::Rejected;
    }
    return Out::None;
  }
  if (eg_.pressed() && !fired_eg_ && now_ms - eg_.pressed_since() >= kBellMinHoldMs + kBellBothGuardMs) {
    fired_eg_ = true;
    return Out::RingEG;
  }
  if (og_.pressed() && !fired_og_ && now_ms - og_.pressed_since() >= kBellMinHoldMs + kBellBothGuardMs) {
    fired_og_ = true;
    return Out::RingOG;
  }
  return Out::None;
}

// ---- DoorKey (indoor) ------------------------------------------------------------------------------------

DoorKey::Out DoorKey::update(uint32_t raw, uint32_t now_ms) {
  TouchChannel::Event e = ch_.update(raw, now_ms);
  if (e == TouchChannel::Event::Pressed) tick_given_ = false;

  if (e == TouchChannel::Event::Stuck) {
    rejected_++;  // lay on the key: cloth, towel, leaning - never a door command
    tick_given_ = false;
    return Out::Rejected;
  }
  if (ch_.pressed()) {
    uint32_t held = now_ms - ch_.pressed_since();
    if (!tick_given_ && held >= kHoldMs && !armed(now_ms)) {
      tick_given_ = true;
      return Out::ReleaseNow;
    }
    return Out::None;
  }
  if (e != TouchChannel::Event::Released) return Out::None;

  // Decision on RELEASE.
  uint32_t held = now_ms - ch_.pressed_since();
  bool tap = held >= kDoorMinHoldMs && held < kTapMaxMs;
  bool hold = held >= kHoldMs && held < kHoldMs + kReleaseWindowMs;
  if ((tap && armed(now_ms)) || hold) return Out::Open;
  rejected_++;
  return Out::Rejected;
}

}  // namespace cb
