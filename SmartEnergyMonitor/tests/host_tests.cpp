#include "demo_control.h"
#include "monitor_ui.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace energy;

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const char* field) {
  if (std::abs(actual - expected) <= tolerance) return;
  std::ostringstream message;
  message << field << " expected " << expected << ", observed " << actual;
  throw std::runtime_error(message.str());
}

std::string escapeXml(const std::string& input) {
  std::string output;
  for (char c : input) {
    if (c == '&') output += "&amp;";
    else if (c == '<') output += "&lt;";
    else if (c == '>') output += "&gt;";
    else output += c;
  }
  return output;
}

struct Segment { int x0, y0, x1, y1; };

class CapturedScreen : public Canvas {
 public:
  std::string labels;
  std::vector<Segment> lines;
  std::ostringstream svg;

  void text(int x, int y, const char* value) override {
    const std::string label(value);
    require(x >= 0 && y >= 0 && x + static_cast<int>(label.size()) * 6 <= 129 &&
            y + 8 <= 64, "OLED label exceeds the display: " + label);
    labels += label + " ";
    // Use the OLED's character-cell spacing. Browser glyph shapes are approximate.
    for (std::size_t i = 0; i < label.size(); ++i) {
      if (label[i] == ' ') continue;
      svg << "<text x=\"" << x + 6 * i << "\" y=\"" << y + 7 << "\">"
          << escapeXml(label.substr(i, 1)) << "</text>\n";
    }
  }

  void line(int x0, int y0, int x1, int y1) override {
    require(x0 >= 0 && x0 < 128 && x1 >= 0 && x1 < 128 &&
            y0 >= 0 && y0 < 64 && y1 >= 0 && y1 < 64,
            "OLED line exceeds the display");
    lines.push_back({x0, y0, x1, y1});
    svg << "<path d=\"M" << x0 + 0.5 << " " << y0 + 0.5
        << " L" << x1 + 0.5 << " " << y1 + 0.5 << "\" stroke=\"white\" stroke-linecap=\"square\"/>\n";
  }

  bool contains(const std::string& phrase) const {
    return labels.find(phrase) != std::string::npos;
  }

  void save(const std::string& directory, const char* name, const char* title) const {
    if (directory.empty()) return;
    const std::string path = directory + "/" + name;
    std::ofstream out(path.c_str(), std::ios::binary);
    require(static_cast<bool>(out), "could not create " + path);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"512\" height=\"256\""
        << " viewBox=\"0 0 128 64\">\n<title>" << escapeXml(title) << "</title>\n"
        << "<desc>Host rendering of OLED draw calls. The font approximates the device glyphs.</desc>\n"
        << "<rect width=\"128\" height=\"64\" fill=\"black\"/>\n"
        << "<g fill=\"white\" font-family=\"monospace\" font-size=\"8\">\n"
        << svg.str() << "</g>\n</svg>\n";
    out.close();
    require(static_cast<bool>(out), "could not finish writing " + path);
  }
};

// Poll at the proposal's 10 ms cadence. Count each command actually delivered.
struct ButtonRig {
  MonitorUi ui;
  std::uint32_t now;
  int acks = 0;
  int rearms = 0;

  explicit ButtonRig(std::uint32_t start = 0) : now(start) { ui.buttons(now, false, false); }

  void hold(int milliseconds, bool pageDown, bool ackDown) {
    for (int t = 0; t < milliseconds; t += 10) {
      now += 10;
      const Action action = ui.buttons(now, pageDown, ackDown);
      if (action == Action::Acknowledge) ++acks;
      if (action == Action::Rearm) ++rearms;
    }
  }

  void nextPage() {
    hold(60, true, false);
    hold(60, false, false);
  }
};

void testPageButton() {
  ButtonRig rig;
  require(rig.ui.page() == 0, "startup page must be the live graph");
  rig.hold(20, true, false);
  rig.hold(40, false, false);
  require(rig.ui.page() == 0, "short contact bounce advanced the page");
  rig.hold(20, true, false);
  rig.hold(10, false, false);
  rig.hold(1000, true, false);
  require(rig.ui.page() == 1, "held press must advance just once");
  rig.hold(60, false, false);
  rig.nextPage();
  require(rig.ui.page() == 2, "second press must select the budget page");
  rig.nextPage();
  require(rig.ui.page() == 0, "third press must wrap to live power");
}

void testAcknowledgeAndRearm() {
  ButtonRig rig(UINT32_MAX - 500);
  rig.hold(20, false, true);
  rig.hold(60, false, false);
  require(rig.acks == 0 && rig.rearms == 0, "bounce emitted an alarm action");
  rig.hold(200, false, true);
  require(rig.acks == 0, "short-press ACK was sent before release");
  rig.hold(60, false, false);
  require(rig.acks == 1 && rig.rearms == 0, "short release must send only ACK");
  rig.hold(1980, false, true);
  require(rig.acks == 1 && rig.rearms == 0, "rearm fired before two seconds");
  rig.hold(200, false, true);
  require(rig.rearms == 1, "long hold across millis rollover must send rearm");
  rig.hold(2100, false, true);
  rig.hold(80, false, false);
  require(rig.acks == 1 && rig.rearms == 1, "long hold repeated or sent ACK on release");
}

std::vector<Segment> graphTrace(const CapturedScreen& screen) {
  std::vector<Segment> result;
  for (const Segment& line : screen.lines) {
    if (line.x0 >= 6 && line.x1 <= 124 && line.x1 - line.x0 == 2 &&
        line.y0 >= 22 && line.y0 <= 41 && line.y1 >= 22 && line.y1 <= 41)
      result.push_back(line);
  }
  return result;
}

void testRollingGraph() {
  MonitorUi ui;
  Snapshot state{};
  const std::uint32_t start = UINT32_MAX - 2500;
  // Five display updates per second must not consume five history slots.
  for (std::uint32_t t = 0; t <= 59000; t += 200) {
    state.circuits[0].watts = t < 1000 ? 5000.0f : 0.0f;
    ui.sample(start + t, state);
  }
  CapturedScreen before;
  ui.draw(before, state);
  const auto oldTrace = graphTrace(before);
  require(oldTrace.size() == 59, "60 samples must produce 59 graph segments");
  require(oldTrace.front().y0 < oldTrace.front().y1, "first second of history disappeared early");
  require(oldTrace.back().x1 == 124, "newest graph sample was not at the right edge");
  ui.sample(start + 60000, state);
  CapturedScreen after;
  ui.draw(after, state);
  const auto rolled = graphTrace(after);
  require(rolled.size() == 59, "rolling graph changed its history size");
  for (const Segment& segment : rolled)
    require(segment.y0 == 41 && segment.y1 == 41, "evicted power spike remains on screen");
}

void testNormalLoad() {
  DemoControl control;
  const float amps[3] = {1.0f, 2.0f, 0.5f};
  for (std::uint32_t t = 0; t <= 60000; t += 100) control.step(t, amps);
  const Snapshot& state = control.snapshot();
  const double powers[3] = {120, 240, 60};
  const double energies[3] = {0.002, 0.004, 0.001};
  double energy = 0, cost = 0;
  for (unsigned i = 0; i < 3; ++i) {
    const Circuit& circuit = state.circuits[i];
    require(circuit.state == CircuitState::On, "normal circuit was not enabled");
    near(circuit.watts, powers[i], 0.001, "watts");
    near(circuit.kwh, energies[i], energies[i] * 0.02, "kWh");
    near(circuit.cost, energies[i] * 0.15, 0.000001, "circuit cost");
    energy += circuit.kwh;
    cost += circuit.cost;
  }
  near(energy, 0.007, 0.00014, "total kWh");
  near(cost, 0.00105, 0.000001, "unrounded total cost");
  std::cout << "    K/H/L watts = 120/240/60, total energy = " << energy
            << " kWh, unrounded cost = $" << cost << "\n";
}

void testBudgetBoundaries(const std::string& evidence) {
  // At a steady 1200 W, one elapsed second is $0.00005. Independent clocks
  // avoid accumulated floating-point roundoff at the exact two thresholds.
  const float amps[3] = {10, 0, 0};
  struct Checkpoint {
    std::uint32_t t;
    double dollars;
    bool warning, alert;
    const char* capture;
  };
  const Checkpoint points[] = {
    {479999000, 23.99995, false, false, nullptr},
    {480000000, 24.00000, true, false, "budget-warning.svg"},
    {599999000, 29.99995, true, false, nullptr},
    {600000000, 30.00000, true, true, "budget-alert.svg"}
  };
  for (const Checkpoint& point : points) {
    DemoControl control;
    control.step(0, amps);
    control.step(point.t, amps);
    const Snapshot& state = control.snapshot();
    near(state.circuits[0].cost, point.dollars, 1e-8, "unrounded threshold cost");
    near(state.budgetPercent, point.dollars / 30.0 * 100.0, 1e-5, "budget percentage");
    require(state.budgetWarning == point.warning && state.budgetAlert == point.alert,
            "warning or alert changed at the wrong dollar amount");
    require(control.buzzerActive() == point.alert && !control.overloadAudible(),
            "100% budget alert must be audible and distinct from overload");
    for (const Circuit& circuit : state.circuits)
      require(circuit.state == CircuitState::On, "budget event tripped a relay");
    if (point.capture) {
      MonitorUi ui;
      ui.sample(point.t, state);
      CapturedScreen screen;
      ui.draw(screen, state);
      require(screen.contains(point.alert ? "BUD100%" : "BUD80%"), "missing OLED budget reason");
      screen.save(evidence, point.capture, point.alert ? "Budget alert at $30" : "Budget warning at $24");
    }
    if (point.alert) {
      control.step(point.t + 100, amps, Action::Acknowledge);
      control.step(point.t + 200, amps);
      require(!control.buzzerActive() && control.snapshot().budgetAlert,
              "ACK must silence the current budget event and keep its flag");
    }
  }
  std::cout << "    $23.99995 clear; $24.00000 warning; $29.99995 warning only; "
               "$30.00000 audible alert. All relays stayed ON.\n";
}

void testStartupBounds() {
  DemoControl control;
  for (const Circuit& circuit : control.snapshot().circuits)
    require(circuit.state == CircuitState::Off && circuit.watts == 0, "relay energized before first frame");
  float amps[3] = {13.999f, 14.0f, 14.001f};
  control.step(0, amps);
  const Snapshot& boot = control.snapshot();
  require(boot.circuits[0].state == CircuitState::On, "demand just below 14 A did not start");
  for (int i = 1; i <= 2; ++i)
    require(boot.circuits[i].state == CircuitState::Trip && boot.circuits[i].watts == 0,
            "startup demand at or above 14 A did not latch off");
  require(control.overloadAudible(), "startup trip did not raise an alarm");
  control.step(2000, amps, Action::Rearm);
  require(control.snapshot().circuits[1].state == CircuitState::Trip,
          "rearm at exactly 14 A bypassed the strict lower threshold");
  std::cout << "    Startup demand 13.999 / 14.000 / 14.001 A produced ON / TRIP / TRIP.\n";
}

void testTransitionEnergy() {
  DemoControl control;
  float amps[3] = {5, 0, 0};
  control.step(0, amps);  // 600 W for 0..100 ms
  amps[0] = 16;
  control.step(100, amps);  // 1920 W for 100..300 ms
  control.step(200, amps);
  control.step(300, amps);
  const double beforeTrip = (600.0 * 100 + 1920.0 * 200) / 3600000000.0;
  const auto& kitchen = control.snapshot().circuits[0];
  require(kitchen.state == CircuitState::Trip, "fixture did not trip at 300 ms");
  near(kitchen.kwh, beforeTrip, 1e-12, "energy up to the trip edge");
  near(kitchen.cost, beforeTrip * 0.15, 1e-12, "cost up to the trip edge");
  amps[0] = 5;
  control.step(400, amps);
  control.step(1400, amps, Action::Rearm);
  require(kitchen.state == CircuitState::On && kitchen.watts == 600, "rearm did not restore 600 W");
  near(kitchen.kwh, beforeTrip, 1e-12, "off interval charged at rearm edge");
  control.step(1500, amps);
  near(kitchen.kwh, beforeTrip + 600.0 * 100 / 3600000000.0, 1e-12, "post-rearm energy");
  std::cout << "    Accounted for 100 ms at 600 W and 200 ms at 1920 W before the trip; "
               "the off interval added zero kWh.\n";
}

void testInvalidSamples() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float badSamples[] = {nan, -0.1f, 20.1f};
  for (const float invalid : badSamples) {
    DemoControl control;
    float amps[3] = {5, 2, 1};
    control.step(0, amps);
    amps[0] = invalid;
    control.step(100, amps);
    const auto& circuits = control.snapshot().circuits;
    require(circuits[0].state == CircuitState::Trip && circuits[0].watts == 0 &&
            control.overloadAudible(), "invalid running sample did not isolate its circuit");
    require(circuits[1].state == CircuitState::On && circuits[2].state == CircuitState::On,
            "invalid sample tripped another circuit");
    near(circuits[0].kwh, 600.0 * 100 / 3600000000.0, 1e-12, "last valid interval");
    amps[0] = 5;
    control.step(200, amps);
    amps[0] = invalid;
    control.step(1000, amps);
    amps[0] = 5;
    control.step(1100, amps);
    control.step(2000, amps, Action::Rearm);
    require(circuits[0].state == CircuitState::Trip, "invalid sample did not interrupt safe dwell");
    control.step(2100, amps, Action::Rearm);
    require(circuits[0].state == CircuitState::On && std::isfinite(circuits[0].cost),
            "circuit did not recover cleanly after one uninterrupted safe second");
  }
  std::cout << "    Missing (NaN), negative and above-range inputs isolated only kitchen; "
               "an invalid sample restarted its safe-demand timer.\n";
}

void testRestart() {
  // Recreate both firmware owners to exercise the RAM-only boot path on the host.
  {
    DemoControl previous;
    float amps[3] = {10, 2, 1};
    previous.step(0, amps);
    previous.step(600000000u, amps);
    amps[1] = 16;
    for (int i = 1; i <= 3; ++i) previous.step(600000000u + i * 100, amps);
    require(previous.snapshot().budgetAlert && previous.overloadAudible() &&
            previous.snapshot().circuits[1].state == CircuitState::Trip,
            "pre-restart alarm, budget, and trip fixture did not activate");
    ButtonRig oldUi;
    oldUi.ui.sample(0, previous.snapshot());
    oldUi.ui.sample(1000, previous.snapshot());
    CapturedScreen oldGraph;
    oldUi.ui.draw(oldGraph, previous.snapshot());
    require(!graphTrace(oldGraph).empty(), "pre-restart history fixture is empty");
    oldUi.nextPage();
    require(oldUi.ui.page() == 1, "pre-restart UI did not advance to page two");
  }
  DemoControl fresh;
  MonitorUi freshUi;
  require(freshUi.page() == 0 && !fresh.buzzerActive(), "page or audible event survived restart");
  for (const Circuit& circuit : fresh.snapshot().circuits)
    require(circuit.state == CircuitState::Off && circuit.watts == 0 &&
            circuit.kwh == 0 && circuit.cost == 0, "initial RAM state was not zero and OFF");
  const float atBoot[3] = {2, 14, 1};
  fresh.step(250, atBoot);
  const Snapshot& state = fresh.snapshot();
  require(state.circuits[0].state == CircuitState::On && state.circuits[1].state == CircuitState::Trip &&
          state.circuits[2].state == CircuitState::On, "new boot did not evaluate fresh demand");
  require(!state.budgetWarning && !state.budgetAlert && state.budgetPercent == 0 &&
          !state.timeValid && !state.networkOnline, "budget or online state survived restart");
  for (const Circuit& circuit : state.circuits)
    require(circuit.kwh == 0 && circuit.cost == 0, "first boot frame charged pre-start time");
  freshUi.sample(250, state);
  CapturedScreen newGraph;
  freshUi.draw(newGraph, state);
  require(graphTrace(newGraph).empty() && newGraph.contains("TRIP HVAC"), "new display retained old history or trip");
  std::cout << "    Recreated controller and UI: cleared kWh, costs, budget, history and page; "
               "first frame applied the new demand and tripped HVAC at 14 A.\n";
}

void testOverload(const std::string& evidence) {
  DemoControl control;
  float amps[3] = {5.0f, 2.0f, 0.5f};
  control.step(0, amps);
  amps[0] = 16;
  control.step(100, amps);
  control.step(200, amps);
  require(control.snapshot().circuits[0].state == CircuitState::On, "tripped before three frames");
  control.step(300, amps);
  const Snapshot atTrip = control.snapshot();
  require(atTrip.circuits[0].state == CircuitState::Trip && atTrip.circuits[0].watts == 0,
          "kitchen did not latch off with zero delivered watts");
  require(control.buzzerActive(), "trip did not sound the buzzer");
  for (int i = 1; i <= 2; ++i)
    require(atTrip.circuits[i].state == CircuitState::On, "kitchen overload interrupted another circuit");
  MonitorUi ui;
  ui.sample(300, atTrip);
  CapturedScreen alert;
  ui.draw(alert, atTrip);
  require(alert.contains("TRIP") && (alert.contains("Kitchen") || alert.contains("KITCHEN")),
          "live display did not identify the tripped kitchen circuit: " + alert.labels);
  alert.save(evidence, "kitchen-trip.svg", "Kitchen overload on the live page");

  amps[0] = 12;
  control.step(400, amps, Action::Acknowledge);
  require(!control.buzzerActive() && control.snapshot().circuits[0].state == CircuitState::Trip,
          "ACK must silence the alarm while retaining the trip");
  for (std::uint32_t t = 500; t <= 1300; t += 100) control.step(t, amps, Action::Rearm);
  const Snapshot stillTripped = control.snapshot();
  require(stillTripped.circuits[0].state == CircuitState::Trip, "rearmed before one safe second");
  near(stillTripped.circuits[0].kwh, atTrip.circuits[0].kwh, 1e-12, "tripped kitchen kWh");
  require(stillTripped.circuits[1].kwh > atTrip.circuits[1].kwh, "healthy circuit energy stopped");
  control.step(1400, amps);
  require(control.snapshot().circuits[0].state == CircuitState::Trip, "safe demand cleared latch by itself");
  control.step(1500, amps, Action::Rearm);
  require(control.snapshot().circuits[0].state == CircuitState::On, "eligible kitchen did not rearm");
  amps[1] = 16;
  for (std::uint32_t t = 1600; t <= 1800; t += 100) control.step(t, amps);
  require(control.snapshot().circuits[1].state == CircuitState::Trip && control.buzzerActive(),
          "new overload after ACK did not sound a fresh alarm");
  std::cout << "    Kitchen tripped on frame 3, ACK preserved its latch, local rearm restored it, "
               "and a subsequent HVAC trip sounded again.\n";
}

void testScreens(const std::string& evidence) {
  ButtonRig rig;
  Snapshot state{};
  state.circuits[0] = {420.0f, 42.125, 6.50, CircuitState::On};
  state.circuits[1] = {0.0f, 65.250, 11.40, CircuitState::Off};
  state.circuits[2] = {90.0f, 17.500, 2.80, CircuitState::On};
  state.tariff = Tariff::Peak;
  state.timeValid = true;
  state.networkOnline = true;
  state.tierActive = true;
  state.budgetPercent = 69.0f;
  // Inject a varying delivered kitchen load to exercise real graph drawing.
  for (std::uint32_t i = 0; i < 60; ++i) {
    state.circuits[0].watts = i < 20 ? 420.0f : (i < 40 ? 1380.0f : 780.0f);
    rig.ui.sample(i * 1000, state);
  }
  CapturedScreen live;
  rig.ui.draw(live, state);
  require(live.contains("LIVE POWER") && live.contains("NET ON") &&
          live.contains("870"), "live page lacks aggregate watts or network state: " + live.labels);
  live.save(evidence, "live-power.svg", "Live aggregate power and 60-second history");

  rig.nextPage();
  CapturedScreen circuits;
  rig.ui.draw(circuits, state);
  require(circuits.contains("CIRCUIT TOTALS") && circuits.contains("Kitchen") &&
          circuits.contains("HVAC") && circuits.contains("Lights") && circuits.contains("OFF") &&
          circuits.contains("ON") && circuits.contains("780 W") && circuits.contains("90 W") &&
          circuits.contains("42.1250 kWh") && circuits.contains("$6.50") &&
          circuits.contains("65.2500 kWh") && circuits.contains("$11.40") &&
          circuits.contains("17.5000 kWh") && circuits.contains("$2.80"),
          "per-circuit page omitted circuit names, powers or states: " + circuits.labels);
  circuits.save(evidence, "circuit-totals.svg", "Kitchen, HVAC and lights totals");

  rig.nextPage();
  CapturedScreen budget;
  rig.ui.draw(budget, state);
  require(budget.contains("COST AND BUDGET") && budget.contains("$20.70") && budget.contains("69.0%") &&
          budget.contains("Peak") && budget.contains("Surcharge") && budget.contains("Verified"),
          "budget page lacks total cost, budget use, tariff, tier or time: " + budget.labels);
  budget.save(evidence, "cost-budget.svg", "Cost, budget, tariff, tier and time status");
  state.timeValid = false;
  state.tierActive = false;
  state.tariff = Tariff::OffPeak;
  CapturedScreen estimate;
  rig.ui.draw(estimate, state);
  require(estimate.contains("Estimated cost") && estimate.contains("Base") && estimate.contains("Off-peak"),
          "budget page did not render unverified time, base tier and off-peak tariff: " + estimate.labels);
  rig.nextPage();
  state.networkOnline = false;
  state.budgetAlert = true;
  state.anomaly = true;
  for (Circuit& circuit : state.circuits) circuit.state = CircuitState::Trip;
  CapturedScreen combined;
  rig.ui.draw(combined, state);
  require(combined.contains("Kitchen") && combined.contains("HVAC") && combined.contains("Lts") &&
          combined.contains("BUD100%") && combined.contains("ANOM") && combined.contains("NET OFF"),
          "concurrent faults lost an active reason: " + combined.labels);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) { std::cerr << "Usage: host_tests [existing-evidence-directory]\n"; return 2; }
  const std::string evidence = argc == 2 ? argv[1] : "";
  int passed = 0, failed = 0;
  auto run = [&](const char* name, const auto& execute) {
    try { execute(); ++passed; std::cout << "PASS  " << name << "\n"; }
    catch (const std::exception& ex) { ++failed; std::cerr << "FAIL  " << name << " - " << ex.what() << "\n"; }
  };
  std::cout << "Smart Energy Monitor | desktop host acceptance tests\n";
  run("PAGE debounce, held-button behavior and page cycling", testPageButton);
  run("ACK/rearm debounce, event timing and timer rollover", testAcknowledgeAndRearm);
  run("60-second rolling graph at 5 Hz redraw cadence", testRollingGraph);
  run("60 seconds of known three-circuit loads at $0.15/kWh", testNormalLoad);
  run("Budget warnings at $24 and $30 without relay disconnection", [&] { testBudgetBoundaries(evidence); });
  run("Safe startup and the strict 14 A boundary", testStartupBounds);
  run("Correct energy intervals across trip and rearm transitions", testTransitionEnergy);
  run("Isolation and recovery after invalid or missing current input", testInvalidSamples);
  run("Offline simulation restart clears RAM and reevaluates demand", testRestart);
  run("Trip, acknowledgement, rearm and new alarm", [&] { testOverload(evidence); });
  run("Three OLED pages, values, states and bounds", [&] { testScreens(evidence); });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
