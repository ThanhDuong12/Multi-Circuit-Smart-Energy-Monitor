#pragma once

#include <stdint.h>
#include "monitor_types.h"

namespace energy {

// Offline data source for exercising the UI while the team's providers are integrated.
class DemoControl {
 public:
  void step(uint32_t nowMs, const float requestedAmps[3], Action action = Action::None);
  void acknowledgeCurrentEvent() { audibleEvents_ = 0; }
  const Snapshot& snapshot() const { return snapshot_; }
  bool buzzerActive() const { return audibleEvents_ != 0; }
  bool overloadAudible() const { return (audibleEvents_ & 0x07u) != 0; }

 private:
  Snapshot snapshot_{};
  uint8_t overloadFrames_[3]{};
  bool cooling_[3]{};
  uint32_t coolSince_[3]{};
  uint8_t audibleEvents_ = 0;
  uint32_t previousMs_ = 0;
  bool started_ = false;
};

}  // namespace energy
