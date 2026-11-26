# Display and control acceptance record

**November 26, 2026.** The 11 desktop test groups passed using the same UI and offline control code as the ESP32 sketch. The host harness supplies precise currents and time so accounting and button boundaries can be checked repeatedly. It also saves drawings from the UI’s actual drawing calls. The SVGs use a desktop font sized to the OLED character cells. [Full test output](evidence/host-test-results.txt)

## Recorded results

The ESP32 Dev Module firmware compiled with Arduino CLI 1.3.1, Arduino-ESP32 2.0.17, Adafruit GFX 1.11.11, BusIO 1.17.4, and SSD1306 2.5.13. The build used 306,541 bytes of program storage and 22,464 bytes of global RAM. The merged Wokwi binary has SHA-256 `acd12d31b291a7ad068aa7643163d596998ec406ff3879b5bb31fc44e5099513`.

In a separate run of the compiled firmware on the team's Wokwi circuit, the OLED cycled through all three pages. The circuit page displayed 120 W, 239 W and 61 W. Raising kitchen demand produced 1,918.8 W before the trip. After the trip, the serial output showed kitchen at 0.0 W with energy holding at 0.00035569 kWh across two successive readings. HVAC and lights remained on. A short ACK changed the audible alarm flag from 1 to 0 while kitchen stayed tripped. After demand was lowered, a sustained red-button hold restored kitchen to ON at 600.7 W.

The [Wokwi session record](evidence/wokwi-session.txt) includes the slider settings, screen readings, and UART samples before and after each alarm action.

| Check | Observed result | Evidence |
| --- | --- | --- |
| Normal loads for 60 simulated seconds | Kitchen 1.00 A → 120 W and 0.002 kWh. HVAC 2.00 A → 240 W and 0.004 kWh. Lights 0.50 A → 60 W and 0.001 kWh. Total 0.007 kWh and $0.00105 at $0.15/kWh. | [Host test log](evidence/host-test-results.txt) |
| PAGE, ACK, and REARM | Contact bounce was rejected. PAGE advanced once per press and wrapped after three pages. A short ACK issued one acknowledge command. A two-second hold spanning a timer rollover issued one rearm command. | [Button tests](tests/host_tests.cpp) |
| Graph and totals | Repeated 5 Hz redraws retained 60 one-second samples. The oldest sample expired after 60 seconds. Each circuit row showed delivered watts, energy, cost, and state. | [Live graph](evidence/live-power.svg) · [Circuit totals](evidence/circuit-totals.svg) |
| Rates and time on the OLED | An injected verified peak/tier snapshot showed $20.70 and 69.0% of budget. An offline snapshot showed off-peak pricing, base tier and estimated cost. | [Cost page](evidence/cost-budget.svg) |
| Overload and recovery | A 16 A kitchen input tripped on the third 100 ms frame. Kitchen power became zero and its energy held steady. The other circuits stayed on. ACK silenced the event. Safe demand alone left the latch set. Local rearm restored kitchen, and a later HVAC trip sounded again. | [Kitchen trip](evidence/kitchen-trip.svg) · [Host test log](evidence/host-test-results.txt) |
| Budget thresholds | $23.99995 was clear, $24.00000 warned, $29.99995 remained warning-only, and $30.00000 triggered the audible alert. All three relays stayed enabled. | [Warning](evidence/budget-warning.svg) · [Alert](evidence/budget-alert.svg) |
| Startup, bad input, and restart | At boot, 13.999 A enabled, while 14.000 and 14.001 A tripped. Invalid samples isolated the affected circuit and restarted its safe-demand timer. Recreating the controller and UI cleared totals and graph history, then evaluated startup demand again. | [Host test log](evidence/host-test-results.txt) |

## Reproduce the evidence

From this sketch directory, build and run with a C++17 compiler. With the optional `evidence` argument, the executable regenerates the six host SVGs in that directory. It exits with an error if an assertion fails.

```sh
c++ -std=c++17 -Wall -Wextra -I. tests/host_tests.cpp monitor_ui.cpp demo_control.cpp -o host_tests
./host_tests evidence
```

On Windows, use the x64 Native Tools Command Prompt for Visual Studio.

```bat
cl /nologo /EHsc /W4 /std:c++17 /I. tests\host_tests.cpp monitor_ui.cpp demo_control.cpp /Fe:host_tests.exe
host_tests.exe evidence
```

The [Wokwi scenario](tests/wokwi-ui.yaml) exercises the actual GPIO pins, the simulated SSD1306 and the serial output. With a configured Wokwi CLI token, run the following from the sketch directory after compiling. The scenario captures the three pages, kitchen trip, and successful rearm.

```sh
wokwi-cli . --scenario tests/wokwi-ui.yaml --timeout 30000 --serial-log-file evidence/wokwi-serial.log
```

## Team integration checks

Use the November 29 integration build for the following checks. Ming records the build revision, input or timestamp, expected and actual results, and the log or screenshot for each case. If a check fails, add the owner, fix revision and retest result. Keep a pending case open until its evidence is attached.

| Case and owner | Test input and pass criterion | Status |
| --- | --- | --- |
| Shared measurements and telemetry · Thanh and Pham | Run the 60-second known inputs above, then sweep the knobs. Compare the copied frame and MQTT with OLED rounding. Check the 100 ms sampling period and numerical RMS reference cases. | Pending integrated sampling and MQTT |
| Tariff, tier and calendar · Pham | Move combined energy from 99.99 to 100.01 kWh off-peak. Cost for that increment should be $0.0035. Check a peak boundary, invalid clock, a single monthly reset, and prospective rate changes. | Pending integrated billing and NTP |
| Alarm priority and anomaly · Thanh and Pham | Warm up 20 frames at 2 A, then apply 6 A for three frames. Check the advisory, simultaneous overload priority, budget notifications at $24/$30, and the corresponding OLED flags. | Display fixtures and offline budget passed. Integrated detection pending. |
| Remote commands and reconnect · Pham and Thanh | Try remote ON during a trip, billing reset, rate changes, and invalid, retained and duplicate commands. Disconnect for 30 seconds. Local display, protection and accounting should continue. Reconnect and check state and command acknowledgements. | Pending integrated MQTT |
| Team rehearsal · all | Run the normal, overload, and remote-control cases in sequence on the reviewed build. Record any issues on November 30. Thanh submits the reviewed release after the December 2 demo. | Pending shared build and team review |
