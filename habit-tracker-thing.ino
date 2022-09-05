/*
  Board: DOIT ESP32 DEVKIT V1
  I2C Pins: D21 (SDA) / D22 (SCL)
  Screen API: https://github.com/ThingPulse/esp8266-oled-ssd1306

  Proposed screen layout is in screen-reference.png
*/

/*
    To Do:
  Create variables to store habit data
  Save habit data to eeprom
  Display data on screen
  OLED saver code
  Button to input data
  Time check on button input
*/

#include <WiFi.h>
#include <time.h>
#include <sntp.h>
#include "WiFiSettings.h"
#include <Wire.h>
#include <SSD1306Wire.h>

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK  "your-password"
#endif

#define LED 5
#define BUTTON 23
#define DEBOUNCE_DELAY 50
#define TIME_CHECK_FREQUENCY 1000 // ms

SSD1306Wire display(0x3c, SDA, SCL);

const char* timeZone = "EST5EDT,M3.2.0,M11.1.0";
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";

const char* ssid = STASSID;
const char* password = STAPSK;

int ledState = HIGH;

int buttonState;
int lastButtonState = LOW;
unsigned long lastDebounceTime = 0;

unsigned long lastTimeCheck = 0;

void setup() {
  Serial.begin(115200);

  configTzTime(timeZone, ntpServer1, ntpServer2);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, ledState);

  display.init();
  display.drawString(0, 0, "Init");
  display.display();

  Serial.println("Setup done");
}

void loop() {
  checkButton();
  checkTime();
  digitalWrite(LED, buttonState);
}

void checkButton() {
  int buttonReading = digitalRead(BUTTON);
  if (buttonReading != lastButtonState) {
      lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (buttonReading != buttonState) {
        buttonState = buttonReading;
    }
  }
  lastButtonState = buttonReading;
}

void checkTime() {
  if ((millis() - lastTimeCheck) > TIME_CHECK_FREQUENCY) {
    lastTimeCheck = millis();
    struct tm timeInfo;
    if(!getLocalTime(&timeInfo)){
      Serial.println("No time available (yet)");
      return;
    }
    Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S");
  }
}
