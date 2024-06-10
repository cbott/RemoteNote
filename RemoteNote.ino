#include <SPI.h>
#include <WiFi101.h>

#include "Adafruit_ThinkInk.h"
#include "ArduinoLowPower.h"

#include "secrets.h"

// Display pins
#define EPD_DC 10
#define EPD_CS 9
#define EPD_BUSY -1 // can set to -1 to not use a pin (will wait a fixed delay)
#define SRAM_CS 6
#define EPD_RESET -1  // can set to -1 and share with microcontroller Reset!
#define EPD_SPI &SPI // primary SPI

// Wifi chip pins
#define ATWINC_CS 8
#define ATWINC_IRQ 7
#define ATWINC_RST 4
#define ATWINC_EN 2

#define LED_PIN 13
#define BUTTON_A_PIN 11

// Refresh time should never be less than 3 minutes due to display limitations
#define REFRESH_TIME_S (60*3)
volatile bool do_deep_sleep = true;

#define BAUD 115200

// 2.9" Tricolor Featherwing or Breakout with IL0373 chipset
ThinkInk_290_Tricolor_Z10 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, EPD_SPI);

// Sensitive data from "secrets.h"
char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
// Google Sheets URL
String url = String("/spreadsheets/d/") + SHEET_ID + "/edit";
char server[] = "docs.google.com";

// Initialize the WiFi client library
WiFiSSLClient client;

void setup() {
  //Configure pins for Adafruit ATWINC1500 Feather
  WiFi.setPins(ATWINC_CS, ATWINC_IRQ, ATWINC_RST, ATWINC_EN);

  Serial.begin(BAUD);

  Serial.println("Initializing Display");
  display.begin(THINKINK_TRICOLOR);

  // Turn off the on-board LED to save power
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Set up an interrupt to allow disabling low power sleep
  // as this sometimes can interfere with flashing
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  LowPower.attachInterruptWakeup(BUTTON_A_PIN, disable_deep_sleep, CHANGE);
}

void loop() {
  // Connect to the WiFi network
  init_wifi();

  // Pull the message from Sheets
  String response = get_line_from_spreadsheet();

  if(!response.length()){
    Serial.println("Failed to retrieve message");
  } else {
    Serial.println("Displaying message...");

    // Clear
    display.clearBuffer();

    // Set background (default: white)
    uint16_t bg_color = EPD_WHITE;
    if(response.startsWith("[BG R]")){
      bg_color = EPD_RED;
      response.remove(0, 6);
    } else if(response.startsWith("[BG B]")){
      bg_color = EPD_BLACK;
      response.remove(0, 6);
    }
    display.fillRect(0, 0, display.width(), display.height(), bg_color);

    // Configure text
    display.setTextSize(3);
    // X position, Y Position measured from top left corner
    display.setCursor(0, 5);

    // Set text color (default: black)
    if(response.startsWith("[R]")){
      display.setTextColor(EPD_RED);
      response.remove(0, 3);
    } else if(response.startsWith("[W]")){
      display.setTextColor(EPD_WHITE);
      response.remove(0, 3);
    } else {
      display.setTextColor(EPD_BLACK);
    }

    display.print(response);

    // Display, sleep=true to power down after
    display.display(true);
  }

  // Disable WiFi chip until next time
  WiFi.end();

  // For some reason low power sleeps were being skipped without
  // a small delay here. 5s arbitrary, <1s likely works fine.
  delay(5000);

  // Wait for a while before next request
  if(do_deep_sleep){
    Serial.println("Starting low power sleep");
    // Disable serial comms for power savings
    Serial.end();
    LowPower.sleep(REFRESH_TIME_S * 1000);
    Serial.begin(BAUD);
    delay(1000); // Allow time to re-establish Serial comms
  } else {
    Serial.println("Starting normal sleep");
    delay(REFRESH_TIME_S * 1000);
  }
  Serial.println("Sleep finished");
}

String get_line_from_spreadsheet(){
  String result;

  // Connect to the Google Sheets API server
  if(client.connect(server, 443)) {
    Serial.println("Connected to server");

    // Make a HTTP GET request
    client.println(String("GET ") + url + " HTTP/1.1");
    client.println("Host: docs.google.com");
    client.println("Connection: close");
    client.println();

    // Wait for a response
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
        Serial.println("Headers received");
        break;
      }
    }

    // Trash the next 3 rows as these are the HTML bits
    for(int i=0; i<3; ++i){
      client.readStringUntil('\n');
    }

    // Read rows of the spreadsheet up until the #END marker
    while (client.available()) {
      String next_line = client.readStringUntil('\n');

      if(next_line.startsWith("#END")){
        break;
      }

      result += next_line + "\n";
    }

    // Disconnect from the server
    client.stop();
  }

  return result;
}

bool init_wifi(){
  // Check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    return false;
  }

  // Attempt to connect to WiFi network
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    delay(10000);
  }

  Serial.println("Connected to WiFi");
  printWiFiStatus();
  return true;
}

void printWiFiStatus() {
  // Print the SSID of the network you're connected to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // Print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // Print the received signal strength
  long rssi = WiFi.RSSI();
  Serial.print("Signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void disable_deep_sleep(){
  // Called as an interrupt
  do_deep_sleep = false;
  // Set the LED on to indicate we are now in a high power state
  digitalWrite(LED_PIN, HIGH);
}
