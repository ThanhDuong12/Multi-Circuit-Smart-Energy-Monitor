# Multi-Circuit-Smart-Energy-Monitor

[Run the Smart Energy Monitor simulation in Wokwi](https://wokwi.com/projects/481151099323724801)

Ming's [display and button demo](SmartEnergyMonitor/README.md) includes a runnable ESP32 sketch, three OLED pages, a 60-second power graph, alarm controls, and [acceptance results](SmartEnergyMonitor/ACCEPTANCE.md). The root sketch and linked Wokwi project are the team's original component check.

Team Information:
- Thanh Hoang Gia Duong
- Doan Huong Thuc Pham
- Ming Ou

Project Description:
Multi Circuit Smart Energy Monitor is an ESP32 and Wokwi educational prototype for three simulated electrical
circuits. It calculates AC RMS from generated samples, estimates watts and kWh, applies time-of-use and tiered
pricing, and reports per-circuit costs. Local protection, a three-page OLED, budget and anomaly alerts, and
two-way MQTT provide monitoring and control. All loads are simulated; accumulated totals are stored in RAM.

Shared project contents:
- C source: sampling, billing, protection, display, and network modules.
- Simulation: diagram.json, wokwi.toml, and build/run instructions.
- Documentation: proposal, figures, pin map, message format, acceptance tests, and evidence.
- README: team roles, setup, progress checklist, known limitations, and demonstration steps.
