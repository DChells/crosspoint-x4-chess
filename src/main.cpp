#include <Arduino.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

#include "ChessPuzzlesApp.h"

HalDisplay display;
HalGPIO gpio;
ChessPuzzlesApp app(display, gpio);

void setup() {
  gpio.begin();

  // Always start serial so debugging works over UART.
  Serial.begin(115200);
  delay(50);
  Serial.printf("[ChessPuzzles] Starting... usb=%d\n", gpio.isUsbConnected() ? 1 : 0);

  app.onEnter();
}

void loop() {
  gpio.update();
  app.loop();
  delay(10);
}
