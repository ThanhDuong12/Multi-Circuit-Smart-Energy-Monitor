#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "demo_control.h"
#include "monitor_ui.h"

constexpr uint8_t kPotPins[] = {32, 33, 34};
constexpr uint8_t kRelayPins[] = {25, 26, 27};
constexpr uint8_t kPagePin = 16, kAckPin = 17, kLedPin = 19, kBuzzerPin = 18;
// Same GPIO convention as the team's starter. Verify the module contacts in Wokwi.
constexpr uint8_t kRelayOn = HIGH, kRelayOff = LOW;

struct Reading {
  energy::Snapshot display;
  bool audible;
  uint32_t atMs;
};

class OledCanvas : public energy::Canvas {
 public:
  explicit OledCanvas(Adafruit_SSD1306& oled) : oled_(oled) {}
  void text(int x, int y, const char* value) override {
    oled_.setCursor(x, y);
    oled_.print(value);
  }
  void line(int x0, int y0, int x1, int y1) override {
    oled_.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
  }
 private:
  Adafruit_SSD1306& oled_;
};

Adafruit_SSD1306 display(128, 64, &Wire, -1);
OledCanvas canvas(display);
energy::MonitorUi ui;
QueueHandle_t actions;
QueueHandle_t latestReading;
SemaphoreHandle_t uiLock;
bool displayReady;
bool running;

static void controlTask(void*);
static void buttonTask(void*);
static void displayTask(void*);
static bool configureBuzzer();
static const char* stateLabel(energy::CircuitState state);

static bool configureBuzzer() {
  ledc_timer_config_t timer{};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_10_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = 1000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer) != ESP_OK) return false;
  ledc_channel_config_t channel{};
  channel.gpio_num = kBuzzerPin;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_0;
  channel.timer_sel = LEDC_TIMER_0;
  channel.duty = 0;
  return ledc_channel_config(&channel) == ESP_OK;
}

void setup() {
  Serial.begin(115200);
  for (uint8_t pin : kRelayPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, kRelayOff);
  }
  pinMode(kPagePin, INPUT_PULLUP);
  pinMode(kAckPin, INPUT_PULLUP);
  pinMode(kLedPin, OUTPUT);
  pinMode(kBuzzerPin, OUTPUT);
  digitalWrite(kLedPin, LOW);
  digitalWrite(kBuzzerPin, LOW);
  if (!configureBuzzer()) {
    Serial.println("Buzzer setup failed; relay outputs remain disabled.");
    return;
  }
  analogReadResolution(12);
  if (Wire.begin(21, 22)) {
    Wire.beginTransmission(0x3C);
    displayReady = Wire.endTransmission() == 0 &&
        display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
  }
  if (displayReady) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    display.display();
  } else {
    Serial.println("OLED unavailable; control and serial trace remain active.");
  }

  latestReading = xQueueCreate(1, sizeof(Reading));
  actions = xQueueCreate(8, sizeof(energy::Action));
  uiLock = xSemaphoreCreateMutex();
  // Start the control owner last. Failed setup leaves all outputs disabled.
  running = latestReading && actions && uiLock &&
      xTaskCreate(buttonTask, "buttons", 3072, nullptr, 2, nullptr) == pdPASS &&
      (!displayReady || xTaskCreate(displayTask, "display", 6144, nullptr, 1, nullptr) == pdPASS) &&
      xTaskCreate(controlTask, "control", 4096, nullptr, 3, nullptr) == pdPASS;
  if (!running) Serial.println("Task setup failed; relay outputs remain disabled.");
  else Serial.println("OFFLINE UI DEMO | 120 V | $0.15/kWh | no NTP or MQTT");
}

static void controlTask(void*) {
  energy::DemoControl control;
  bool buzzerOn = false;
  TickType_t previousWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    float amps[3];
    for (unsigned i = 0; i < 3; ++i) amps[i] = analogRead(kPotPins[i]) * (20.0f / 4095.0f);

    // Buttons only enqueue requests. All relay, alarm, and billing changes occur here.
    energy::Action incoming;
    energy::Action requested = energy::Action::None;
    while (xQueueReceive(actions, &incoming, 0) == pdTRUE) {
      if (incoming == energy::Action::Acknowledge) control.acknowledgeCurrentEvent();
      if (incoming == energy::Action::Rearm) requested = incoming;
    }
    control.step(now, amps, requested);
    const energy::Snapshot& snapshot = control.snapshot();
    bool trip = false;
    for (unsigned i = 0; i < 3; ++i) {
      const auto state = snapshot.circuits[i].state;
      digitalWrite(kRelayPins[i], state == energy::CircuitState::On ? kRelayOn : kRelayOff);
      trip |= state == energy::CircuitState::Trip;
    }
    const bool fastBlink = (now / 200u) % 2u == 0;
    const bool slowBlink = (now / 500u) % 2u == 0;
    const bool ledOn = trip || (snapshot.budgetAlert ? fastBlink :
        (snapshot.budgetWarning || snapshot.anomaly) && slowBlink);
    digitalWrite(kLedPin, ledOn ? HIGH : LOW);
    const bool sound = control.buzzerActive() && (control.overloadAudible() || fastBlink);
    if (sound != buzzerOn) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, sound ? 512u : 0u);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      buzzerOn = sound;
    }
    const Reading reading{snapshot, control.buzzerActive(), now};
    xQueueOverwrite(latestReading, &reading);
    // Do not count immediate catch-up iterations as separate overload frames.
    const TickType_t finished = xTaskGetTickCount();
    if (finished - previousWake >= pdMS_TO_TICKS(100)) previousWake = finished;
    vTaskDelayUntil(&previousWake, pdMS_TO_TICKS(100));
  }
}

static void buttonTask(void*) {
  TickType_t previousWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    const bool page = digitalRead(kPagePin) == LOW;
    const bool ack = digitalRead(kAckPin) == LOW;
    xSemaphoreTake(uiLock, portMAX_DELAY);
    const energy::Action action = ui.buttons(now, page, ack);
    xSemaphoreGive(uiLock);
    if (action != energy::Action::None) xQueueSend(actions, &action, 0);
    vTaskDelayUntil(&previousWake, pdMS_TO_TICKS(10));
  }
}

static void displayTask(void*) {
  TickType_t previousWake = xTaskGetTickCount();
  for (;;) {
    Reading reading;
    if (xQueuePeek(latestReading, &reading, 0) == pdTRUE) {
      display.clearDisplay();
      xSemaphoreTake(uiLock, portMAX_DELAY);
      ui.sample(reading.atMs, reading.display);
      ui.draw(canvas, reading.display);
      xSemaphoreGive(uiLock);
      // Transfer over I2C outside the lock and independently of button polling.
      display.display();
    }
    vTaskDelayUntil(&previousWake, pdMS_TO_TICKS(200));
  }
}

static const char* stateLabel(energy::CircuitState state) {
  switch (state) {
    case energy::CircuitState::On: return "ON";
    case energy::CircuitState::Trip: return "TRIP";
    default: return "OFF";
  }
}

void loop() {
  // The Arduino loop task owns the UART. Serial output cannot delay the control loop.
  Reading reading;
  if (running && xQueuePeek(latestReading, &reading, 0) == pdTRUE) {
    const char* names[] = {"Kitchen", "HVAC", "Lights"};
    Serial.printf("t=%lu ms audible_alarm=%d", static_cast<unsigned long>(reading.atMs),
                  static_cast<int>(reading.audible));
    for (unsigned i = 0; i < 3; ++i) {
      const auto& circuit = reading.display.circuits[i];
      Serial.printf(" | %s %s %.1f W %.8f kWh $%.8f", names[i], stateLabel(circuit.state),
                    circuit.watts, circuit.kwh, circuit.cost);
    }
    Serial.println();
  }
  delay(1000);
}
