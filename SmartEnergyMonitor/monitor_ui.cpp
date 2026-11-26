#include "monitor_ui.h"

#include <stdio.h>
#include <string.h>

namespace energy {
namespace {

constexpr int screenWidth = 128;
constexpr int cellWidth = 6;
constexpr uint32_t debounceMs = 30;
constexpr uint32_t holdMs = 2000;
constexpr uint32_t sampleMs = 1000;
const char* const circuitNames[] = {"Kitchen", "HVAC", "Lights"};

float totalWatts(const Snapshot& snapshot) {
  float total = 0;
  for (const Circuit& circuit : snapshot.circuits) total += circuit.watts;
  return total;
}

void rightText(Canvas& canvas, int y, const char* value) {
  canvas.text(screenWidth - cellWidth * static_cast<int>(strlen(value)), y, value);
}

void heading(Canvas& canvas, const char* title, const char* pageNumber) {
  canvas.text(0, 0, title);
  rightText(canvas, 0, pageNumber);
  canvas.line(0, 9, 127, 9);
}

const char* stateLabel(CircuitState state) {
  switch (state) {
    case CircuitState::On: return "ON";
    case CircuitState::Off: return "OFF";
    case CircuitState::Trip: return "TRIP";
  }
  return "?";
}

void costText(char* text, size_t capacity, double cost) {
  if (cost < 1000000.0) {
    snprintf(text, capacity, "$%.2f", cost);
  } else {
    snprintf(text, capacity, "$%.2fM", cost / 1000000.0);
  }
}

void drawCircuits(Canvas& canvas, const Snapshot& snapshot) {
  heading(canvas, "CIRCUIT TOTALS", "2/3");
  for (unsigned i = 0; i < 3; ++i) {
    const Circuit& circuit = snapshot.circuits[i];
    const int y = 11 + i * 18;
    canvas.text(0, y, circuitNames[i]);
    canvas.text(48, y, stateLabel(circuit.state));
    char value[24];
    snprintf(value, sizeof(value), "%.0f W", static_cast<double>(circuit.watts));
    rightText(canvas, y, value);
    if (circuit.kwh < 1000.0) {
      snprintf(value, sizeof(value), "%.4f kWh", circuit.kwh);
    } else {
      snprintf(value, sizeof(value), "%.2f MWh", circuit.kwh / 1000.0);
    }
    canvas.text(0, y + 8, value);
    costText(value, sizeof(value), circuit.cost);
    rightText(canvas, y + 8, value);
  }
}

void drawCost(Canvas& canvas, const Snapshot& snapshot) {
  heading(canvas, "COST AND BUDGET", "3/3");
  double total = 0;
  for (const Circuit& circuit : snapshot.circuits) total += circuit.cost;
  char value[24];
  canvas.text(0, 12, "Total");
  costText(value, sizeof(value), total);
  rightText(canvas, 12, value);

  canvas.text(0, 22, "Budget");
  snprintf(value, sizeof(value), "%.1f%%", static_cast<double>(snapshot.budgetPercent));
  rightText(canvas, 22, value);
  canvas.line(0, 31, 127, 31);
  canvas.line(0, 35, 127, 35);
  canvas.line(0, 32, 0, 34);
  canvas.line(127, 32, 127, 34);
  const float percent = snapshot.budgetPercent;
  if (percent > 0) {
    const int pixels = percent >= 100 ? 126 : static_cast<int>(1.26f * percent + 0.5f);
    if (pixels) canvas.line(1, 33, pixels, 33);
  }
  canvas.text(0, 38, "Tariff");
  rightText(canvas, 38, snapshot.tariff == Tariff::Peak ? "Peak" : "Off-peak");
  canvas.text(0, 47, "Tier");
  rightText(canvas, 47, snapshot.tierActive ? "Surcharge" : "Base");
  canvas.text(0, 56, "Time");
  rightText(canvas, 56, snapshot.timeValid ? "Verified" : "Estimated cost");
}

void drawStatus(Canvas& canvas, const Snapshot& snapshot) {
  char trips[24] = "TRIP";
  size_t used = 4;
  for (unsigned i = 0; i < 3; ++i) {
    if (snapshot.circuits[i].state != CircuitState::Trip) continue;
    // The three names fit on one line with a short label for Lights.
    const char* name = i == 2 && used > 13 ? "Lts" : circuitNames[i];
    used += static_cast<size_t>(snprintf(trips + used, sizeof(trips) - used, " %s", name));
  }
  const bool hasTrip = used > 4;
  const bool hasAdvisory = snapshot.budgetWarning || snapshot.budgetAlert || snapshot.anomaly;
  canvas.text(0, 46, hasTrip ? trips : (hasAdvisory ? "Active alerts" : "No active alarms"));

  char flags[24];
  snprintf(flags, sizeof(flags), "%s%s%s",
           snapshot.budgetAlert ? "BUD100% " : (snapshot.budgetWarning ? "BUD80% " : ""),
           snapshot.anomaly ? "ANOM " : "",
           snapshot.networkOnline ? "NET ON" : "NET OFF");
  canvas.text(0, 56, flags);
}

}  // namespace

bool MonitorUi::Button::update(uint32_t nowMs, bool pressed) {
  if (pressed != raw) {
    raw = pressed;
    edgeMs = nowMs;
  }
  if (raw == stable || nowMs - edgeMs < debounceMs) return false;
  stable = raw;
  return true;
}

Action MonitorUi::buttons(uint32_t nowMs, bool pagePressed, bool ackPressed) {
  if (pageButton_.update(nowMs, pagePressed) && pageButton_.stable) {
    page_ = (page_ + 1) % 3;
  }

  if (ackButton_.update(nowMs, ackPressed)) {
    if (!ackButton_.stable) {
      if (longPressSent_) return Action::None;
      // Handle a full hold even if there was no poll exactly at the threshold.
      return ackButton_.edgeMs - ackStartMs_ >= holdMs ? Action::Rearm : Action::Acknowledge;
    }
    ackStartMs_ = ackButton_.edgeMs;
    longPressSent_ = false;
  }
  if (ackButton_.raw && ackButton_.stable && !longPressSent_ && nowMs - ackStartMs_ >= holdMs) {
    longPressSent_ = true;
    return Action::Rearm;
  }
  return Action::None;
}

void MonitorUi::appendWatts(float watts) {
  history_[next_] = watts;
  next_ = (next_ + 1) % historySize_;
  if (count_ < historySize_) ++count_;
}

void MonitorUi::sample(uint32_t nowMs, const Snapshot& snapshot) {
  if (count_ == 0) {
    lastSampleMs_ = nowMs;
  } else {
    const uint32_t slots = (nowMs - lastSampleMs_) / sampleMs;
    if (!slots) return;
    lastSampleMs_ += slots * sampleMs;
    if (slots >= historySize_) {
      next_ = 0;
      count_ = 0;
    } else {
      // A delayed task must leave a gap rather than invent historical readings.
      for (uint32_t i = 1; i < slots; ++i) appendWatts(-1.0f);
    }
  }
  appendWatts(totalWatts(snapshot));
}

void MonitorUi::drawLive(Canvas& canvas, const Snapshot& snapshot) const {
  heading(canvas, "LIVE POWER", "1/3");
  char label[24];
  snprintf(label, sizeof(label), "%.0f W", static_cast<double>(totalWatts(snapshot)));
  canvas.text(0, 12, label);
  rightText(canvas, 12, "60 s trend");
  canvas.line(3, 22, 3, 42);
  canvas.line(3, 42, 125, 42);

  const unsigned oldest = (next_ + historySize_ - count_) % historySize_;
  float peak = 1.0f;
  for (unsigned i = 0; i < count_; ++i) {
    const float watts = history_[(oldest + i) % historySize_];
    if (watts > peak) peak = watts;
  }
  int lastX = -1;
  int lastY = 0;
  for (unsigned i = 0; i < count_; ++i) {
    const float watts = history_[(oldest + i) % historySize_];
    if (watts < 0) {
      lastX = -1;
      continue;
    }
    const int x = 124 - 2 * (count_ - 1 - i);
    const int y = 41 - static_cast<int>(19 * watts / peak + 0.5f);
    canvas.line(lastX < 0 ? x : lastX, lastX < 0 ? y : lastY, x, y);
    lastX = x;
    lastY = y;
  }
  drawStatus(canvas, snapshot);
}

void MonitorUi::draw(Canvas& canvas, const Snapshot& snapshot) const {
  switch (page_) {
    case 0: drawLive(canvas, snapshot); break;
    case 1: drawCircuits(canvas, snapshot); break;
    case 2: drawCost(canvas, snapshot); break;
  }
}

}  // namespace energy
