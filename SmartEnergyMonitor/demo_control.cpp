#include "demo_control.h"

namespace energy {
namespace {
constexpr float kVolts = 120.0f;
constexpr double kRate = 0.15;
constexpr double kBudget = 30.0;
constexpr uint8_t kBudgetEvent = 1u << 3;
}

void DemoControl::step(uint32_t nowMs, const float requestedAmps[3], Action action) {
  const uint32_t elapsedMs = started_ ? nowMs - previousMs_ : 0u;
  previousMs_ = nowMs;
  // A new trip in this frame must still sound if an older event was acknowledged.
  if (action == Action::Acknowledge) acknowledgeCurrentEvent();

  double totalCost = 0.0;
  for (unsigned i = 0; i < 3; ++i) {
    Circuit& circuit = snapshot_.circuits[i];
    // Charge for the state that was actually in effect during the preceding interval.
    const double addedKwh = static_cast<double>(circuit.watts) * elapsedMs / 3600000000.0;
    circuit.kwh += addedKwh;
    circuit.cost += addedKwh * kRate;
    totalCost += circuit.cost;

    const uint8_t event = 1u << i;
    const float amps = requestedAmps[i];
    const bool valid = amps >= 0.0f && amps <= 20.0f;
    const bool lowDemand = valid && amps < 14.0f;
    if (!started_) circuit.state = lowDemand ? CircuitState::On : CircuitState::Trip;

    if (circuit.state == CircuitState::On) {
      overloadFrames_[i] = amps > 15.0f ? overloadFrames_[i] + 1 : 0;
      if (!valid || overloadFrames_[i] >= 3) {
        circuit.state = CircuitState::Trip;
        audibleEvents_ |= event;
      }
    }
    if (circuit.state == CircuitState::Trip) {
      if (!started_) audibleEvents_ |= event;
      if (lowDemand && !cooling_[i]) coolSince_[i] = nowMs;
      cooling_[i] = lowDemand;
      if (action == Action::Rearm && lowDemand && nowMs - coolSince_[i] >= 1000u) {
        circuit.state = CircuitState::On;
        cooling_[i] = false;
        overloadFrames_[i] = 0;
        audibleEvents_ &= static_cast<uint8_t>(~event);
      }
    }
    circuit.watts = circuit.state == CircuitState::On ? kVolts * amps : 0.0f;
  }

  snapshot_.tariff = Tariff::OffPeak;
  snapshot_.budgetPercent = static_cast<float>(100.0 * totalCost / kBudget);
  snapshot_.budgetWarning = totalCost >= kBudget * 0.8;
  if (!snapshot_.budgetAlert && totalCost >= kBudget) audibleEvents_ |= kBudgetEvent;
  snapshot_.budgetAlert = totalCost >= kBudget;
  // Default false: no network, verified clock, tier surcharge or anomaly provider in this demo.
  started_ = true;
}

}  // namespace energy
