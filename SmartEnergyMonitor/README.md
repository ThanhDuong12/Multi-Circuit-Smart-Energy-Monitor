# Smart Energy Monitor display and controls

This is the runnable ESP32 contribution for Ming Ou's display, button interface, and test records. The three pages use one snapshot of the kitchen, HVAC, and lighting circuits. An offline control module supplies the readings until the team connects its sampling, billing, and network code. The team’s [original Wokwi simulation](https://wokwi.com/projects/481151099323724801) contains the earlier component-check sketch.

## Run the demo

**From the handoff ZIP:** extract it, open the [team circuit in Wokwi](https://wokwi.com/projects/481151099323724801), click inside its code editor, and press **F1**. Choose **Upload Firmware and Start Simulation…** and select `build/Ming-Ou-Wokwi.bin` from the extracted sketch folder. This temporarily runs the included build with the team’s wiring. [Wokwi custom firmware instructions](https://docs.wokwi.com/guides/esp32#custom-application-firmware)

**From VS Code:** open the **SmartEnergyMonitor** folder that contains `wokwi.toml`. With Wokwi for VS Code activated, run **Wokwi: Start Simulator** from the command palette. The 115200-baud serial monitor shows each circuit’s watts, energy, cost, state, and the audible-alarm flag.

For a source checkout, use Arduino CLI with the Espressif board index. Run these commands inside this folder. Rebuild after changing the sketch.

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@2.0.17 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install --no-deps "Adafruit BusIO@1.17.4" "Adafruit GFX Library@1.11.11" "Adafruit SSD1306@2.5.13"
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir build .
```

## Demonstrate the interface

| Action | What to show |
| --- | --- |
| Start with all knobs low, then turn one knob. | The live page shows combined delivered watts. The graph adds one point per second and keeps the last 60 seconds. The footer shows alarms and network status. |
| Press and release the green PAGE button once. | Each circuit has its own row with watts, kWh, cost, and ON/OFF/TRIP state. A held button advances just once. |
| Press PAGE again. | The cost page shows total dollars, budget percentage, tariff, tier, and time status. The offline demo shows off-peak pricing and estimated cost. Another press returns to the graph. |
| Set only the kitchen knob to about 80% of travel (16 A nominal). | After three over-limit samples the kitchen trips. Its delivered power falls to zero and its kWh stops increasing. HVAC and lights keep operating. Point out the circuit name, LED, and buzzer. |
| Tap the red ACK/REARM button. | The buzzer stops. The kitchen trip and steady LED remain. A fresh overload on another circuit can sound again. |
| Lower the kitchen knob to about half travel, wait one second, then hold red for two seconds. | An eligible kitchen circuit returns to ON. Lowering the knob alone leaves the trip latched. |

The knobs represent 0–20 A and the offline provider assumes 120 V, a fixed $0.15/kWh, and a $30 budget. The OLED rounds dollars to cents. Use the serial trace for the unrounded acceptance figures. Restarting the simulation clears the RAM totals. The demo is a low-voltage simulation.

## Connect the team firmware

| Owner | Integration |
| --- | --- |
| Thanh Hoang Gia Duong | Publish a copy of `energy::Snapshot` each control frame, ordered kitchen, HVAC, lights. Watts are delivered watts, energy is kWh, cost is dollars. Handle queued `Acknowledge` and `Rearm` commands in the control task, which owns output state and eligibility. |
| Doan Huong Thuc Pham | Supply tariff, tier, budget percentage, clock validity, and network status in the same snapshot. For final acceptance compare OLED and MQTT readings from the same frame, allowing for the OLED’s display precision. |
| Ming Ou | Maintain the three pages, graph, debounce, acceptance records, and demo instructions. Poll buttons every 10 ms, debounce for 30 ms, and redraw at 5 Hz. The OLED transfer runs independently of protection. |

`monitor_types.h` defines the copied data and command types. `monitor_ui.h/.cpp` handle input and rendering. `SmartEnergyMonitor.ino` connects the queues, GPIO, and SSD1306 to the UI. `demo_control.h/.cpp` supply the offline data. The UI can use the team's ESP-IDF firmware through the same snapshot, commands, and two drawing operations.

| Function | Connections on the classic ESP32 |
| --- | --- |
| Kitchen, HVAC, lights knobs | ADC1 GPIO32, GPIO33, GPIO34 |
| OLED | SDA GPIO21, SCL GPIO22, 128 × 64, I²C address 0x3C |
| PAGE and ACK/REARM | GPIO16 and GPIO17, pull-ups, pressed low |
| Kitchen, HVAC, lights relay inputs | GPIO25, GPIO26, GPIO27 |
| Alarm | GPIO19 LED with 330 Ω resistor, GPIO18 buzzer |

See [acceptance results and integration checks](ACCEPTANCE.md) for measured values, display captures, open checks, and instructions to reproduce the tests.
