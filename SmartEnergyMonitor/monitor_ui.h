#pragma once

#include <stdint.h>
#include "monitor_types.h"

namespace energy {

// The caller clears and presents each 128 x 64 frame. Text uses a 6 x 8 cell.
class Canvas {
public:
  virtual ~Canvas() = default;
  virtual void text(int x, int y, const char* value) = 0;
  virtual void line(int x0, int y0, int x1, int y1) = 0;
};

class MonitorUi {
public:
  Action buttons(uint32_t nowMs, bool pagePressed, bool ackPressed);
  void sample(uint32_t nowMs, const Snapshot& snapshot);
  void draw(Canvas& canvas, const Snapshot& snapshot) const;
  uint8_t page() const { return page_; }

private:
  struct Button {
    bool raw = false;
    bool stable = false;
    uint32_t edgeMs = 0;
    bool update(uint32_t nowMs, bool pressed);
  };

  void appendWatts(float watts);
  void drawLive(Canvas& canvas, const Snapshot& snapshot) const;

  Button pageButton_;
  Button ackButton_;
  uint32_t ackStartMs_ = 0;
  bool longPressSent_ = false;
  uint8_t page_ = 0;

  static const uint8_t historySize_ = 60;
  float history_[historySize_] = {};
  uint8_t next_ = 0;
  uint8_t count_ = 0;
  uint32_t lastSampleMs_ = 0;
};

}  // namespace energy
