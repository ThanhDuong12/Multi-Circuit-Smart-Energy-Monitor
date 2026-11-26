#pragma once

namespace energy {

enum class CircuitState { Off, On, Trip };
enum class Tariff { OffPeak, Peak };
enum class Action { None, Acknowledge, Rearm };

struct Circuit {
  float watts;
  double kwh;
  double cost;
  CircuitState state;
};

// One consistent control-frame copy, ordered kitchen, HVAC, lights.
// Cost is in dollars; budgetPercent is 0..100 at or below the budget.
struct Snapshot {
  Circuit circuits[3];
  float budgetPercent;
  Tariff tariff;
  bool tierActive;
  bool timeValid;
  bool networkOnline;
  bool budgetWarning;
  bool budgetAlert;
  bool anomaly;
};

}  // namespace energy
