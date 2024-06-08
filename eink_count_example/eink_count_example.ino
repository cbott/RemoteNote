/***************************************************
  Adapted from Adafruit ThinkInk_tricolor example
  https://learn.adafruit.com/adafruit-2-9-eink-display-breakouts-and-featherwings/arduino-usage
 ****************************************************/

#include "Adafruit_ThinkInk.h"

#define EPD_DC 10
#define EPD_CS 9
#define EPD_BUSY -1 // can set to -1 to not use a pin (will wait a fixed delay)
#define SRAM_CS 6
#define EPD_RESET -1  // can set to -1 and share with microcontroller Reset!
#define EPD_SPI &SPI // primary SPI

// 2.9" Tricolor Featherwing or Breakout with IL0373 chipset
ThinkInk_290_Tricolor_Z10 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, EPD_SPI);

void setup() {
  Serial.begin(115200);
  pinMode(13, OUTPUT);

  Serial.println("E-Ink Counting Example");
  display.begin(THINKINK_TRICOLOR);
}

void loop() {
  Serial.println("Program Start");

  // Toggle the LED to indicate start
  digitalWrite(13, HIGH);
  delay(1000);
  digitalWrite(13, LOW);

  int i = 0;

  while(1){
    // Clear
    display.clearBuffer();

    // Set white background
    display.fillRect(0, 0, display.width(), display.height(), EPD_WHITE);

    // Configure text
    display.setTextSize(3);
    display.setCursor((display.width() - 144) / 2, (display.height() - 24) / 2);
    display.setTextColor(EPD_BLACK);
    display.print("COUNT: ");
    display.setTextColor(EPD_RED);
    display.print(i++);

    // Display
    display.display();

    // Adafruit recommends at least 180 seconds between refreshes
    delay(180000);
  }
}
