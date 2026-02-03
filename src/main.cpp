#include <Arduino.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

#include "ChessPuzzlesApp.h"

HalDisplay display;
HalGPIO gpio;
ChessPuzzlesApp app(display, gpio);

void setup() {
  gpio.begin();

  // Only start serial if USB connected
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    Serial.println("[ChessPuzzles] Starting...");
  }

  app.onEnter();
}

void loop() {
  gpio.update();
  app.loop();
  delay(10);
}
