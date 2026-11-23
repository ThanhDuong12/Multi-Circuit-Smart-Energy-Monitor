

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
const int pots[3] = {32, 33, 34};
const int relays[3] = {25, 26, 27};
const int BTN_PAGE = 16, BTN_ACK = 17, LED = 19, BUZ = 18;

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 3; i++) { pinMode(relays[i], OUTPUT); digitalWrite(relays[i], LOW); }
  pinMode(BTN_PAGE, INPUT_PULLUP);
  pinMode(BTN_ACK, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  pinMode(BUZ, OUTPUT);
  Wire.begin(21, 22);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED not found");
  oled.setTextColor(SSD1306_WHITE);
}

void loop() {
  int v[3];
  for (int i = 0; i < 3; i++) v[i] = analogRead(pots[i]);
  bool page = !digitalRead(BTN_PAGE);
  bool ack  = !digitalRead(BTN_ACK);

  digitalWrite(LED, ack);                          // ACK button lights LED
  for (int i = 0; i < 3; i++)
    digitalWrite(relays[i], v[i] > 2048);          // pot past halfway = relay on
  if (page) tone(BUZ, 1000); else noTone(BUZ);     // PAGE button sounds buzzer

  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.printf("K:%4d\nH:%4d\nL:%4d", v[0], v[1], v[2]);
  oled.display();
  delay(100);
}