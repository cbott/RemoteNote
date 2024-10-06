#include <SPI.h>
#include <WiFi101.h>

#include "Adafruit_ThinkInk.h"
#include "ArduinoLowPower.h"

#include "secrets.h"

// Display pins
#define EPD_DC 10
// ONLY valid if you have modified your FeatherWing to cut the ECS and SDCS jumpers
// and bridged ECS to pin 5, otherwise set EPD_CS=9 and voltage sense will not work
#define EPD_CS 5
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

// Battery voltage measurements
#define VBATPIN A7
#define MIN_BATT_V 3.0
#define MAX_BATT_V 4.2

// Refresh time should never be less than 3 minutes due to display limitations
#define REFRESH_TIME_S (60*60*3)
volatile bool do_deep_sleep = true;

// Serial Baud rate
#define BAUD 115200

// Times to attempt wifi connection before displaying error message
#define MAX_CONNECTION_RETRIES 5
// Delay between connection attempts
#define CONNECTION_RETRY_DELAY_MS 5000

// 2.9" Tricolor Featherwing or Breakout with IL0373 chipset
ThinkInk_290_Tricolor_Z10 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, EPD_SPI);

// Sensitive data from "secrets.h"
char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
// Message server URL
String url = String("/") + SECRET_PAGE_ID;
char server[] = "gist.githubusercontent.com";

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
  String response;

  // Connect to the WiFi network
  if(init_wifi()){
    // Pull the message from the internet
    response = get_text_from_server();
  } else {
    response = "[BG R][W]   WiFi Error   ";
  }

  // Disable WiFi chip until next time for power saving
  WiFi.end();

  if(!response.length()){
    Serial.println("Failed to retrieve message");
    response = "[BG R][W] Message Error  ";
  }

  // Note: had some minor display issues if we called this before Wifi.end()
  parse_and_display_message(response);

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


void parse_and_display_message(String msg){
  Serial.println("Displaying message...");

  // Clear
  display.clearBuffer();

  // Set background (default: white) and battery bar (default: red)
  uint16_t bg_color = EPD_WHITE;
  uint16_t batt_color = EPD_RED;
  if(msg.startsWith("[BG R]")){
    bg_color = EPD_RED;
    batt_color = EPD_WHITE;
    msg.remove(0, 6);
  } else if(msg.startsWith("[BG B]")){
    bg_color = EPD_BLACK;
    msg.remove(0, 6);
  }
  display.fillRect(0, 0, display.width(), display.height(), bg_color);

  // Configure text
  display.setTextSize(3);
  // X position, Y Position measured from top left corner
  display.setCursor(0, 5);

  // Set text color (default: black)
  if(msg.startsWith("[R]")){
    display.setTextColor(EPD_RED);
    msg.remove(0, 3);
  } else if(msg.startsWith("[W]")){
    display.setTextColor(EPD_WHITE);
    msg.remove(0, 3);
  } else {
    display.setTextColor(EPD_BLACK);
  }

  display.print(msg);

  // Display battery indicator at the bottom of the screen
  float vbatt = get_battery_voltage();
  Serial.print("VBat: " ); Serial.println(vbatt);
  display.fillRect(0, display.height() - 2, battery_bar_width(vbatt), 2, batt_color);

  // Display, sleep=true to power down after
  display.display(true);
}


// Returns the full message text from the message server
String get_text_from_server(){
  String result;

  // Connect to the message server
  if(client.connect(server, 443)) {
    Serial.println("Connected to server");

    // Make a HTTP GET request
    client.println(String("GET ") + url + " HTTP/1.1");
    client.print("Host: ");
    client.println(server);
    client.println("Connection: close");
    client.println();

    // Wait for a response
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      Serial.print("Header: ");
      Serial.println(line);
      if (line == "\r") {
        Serial.println("Headers received");
        break;
      }
    }

    // Read all characters from source, up to 100 total or until the #END marker
    Serial.println("Pulling Message");
    delay(10);

    while (client.available()) {
      char c = client.read();
      result += c;
      Serial.write(c);
      delay(1);

      if(result.length() > 100 || result.endsWith("#END")){
        break;
      }
    }

    // Disconnect from the server
    client.stop();
  }
  Serial.println();

  return result;
}


bool init_wifi(){
  // Check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    return false;
  }

  // Attempt to connect to WiFi network
  int n_attempts = 0;
  for(int n_attempts=1; n_attempts<=MAX_CONNECTION_RETRIES; ++n_attempts) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.print(ssid);
    Serial.print(" (");
    Serial.print(n_attempts);
    Serial.print("/");
    Serial.print(MAX_CONNECTION_RETRIES);
    Serial.println(")");

    if(WiFi.begin(ssid, pass) == WL_CONNECTED){
      Serial.println("Connected to WiFi");
      printWiFiStatus();
      return true;
    }

    delay(CONNECTION_RETRY_DELAY_MS);
  }

  Serial.println("Failed to connect");
  return false;
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


float get_battery_voltage(){
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // Correct for /2 voltage divider
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  return measuredvbat;
}


int16_t battery_bar_width(float vbatt){
  // Returns a width for the battery indicator given present voltage (battery_percentage * total_display_width)
  float batt_pct = constrain((vbatt-MIN_BATT_V) / (MAX_BATT_V - MIN_BATT_V), 0.0, 1.0);
  return int16_t(batt_pct * (display.width()));
}
