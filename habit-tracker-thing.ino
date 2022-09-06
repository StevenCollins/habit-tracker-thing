/*
  Board: DOIT ESP32 DEVKIT V1
  I2C Pins: D21 (SDA) / D22 (SCL)
  Screen API: https://github.com/ThingPulse/esp8266-oled-ssd1306
  Time Docs: https://github.com/Patapom/Arduino/blob/master/Libraries/AVR%20Libc/avr-libc-2.0.0/include/time.h

  Proposed screen layout is in screen-reference.png
*/

/*
    To Do:
  Save habit data to eeprom
  OLED saver code
  Valid time check on button input
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
#define TIME_CHECK_FREQUENCY 1000
#define ONE_DAY 86400

SSD1306Wire display(0x3c, SDA, SCL);

const char* timeZone = "EST5EDT,M3.2.0,M11.1.0";
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";

const char* ssid = STASSID;
const char* password = STAPSK;

const int width = 6; // Width of month column
const int height = 2; // Height of each day in a month column
const int border = 1; // Space to edge of screen
const int columnSpace = 3; // Space between columns
const int weekSize = 8; // Size of each week box
const int weekSpace = 1; // Space between week boxes

int ledState = HIGH;

int buttonState;
int lastButtonState = LOW;
unsigned long lastDebounceTime = 0;

struct tm timeInfo;
unsigned long lastTimeCheck = 0;

bool habitData[12][31];

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, ledState);

  configTzTime(timeZone, ntpServer1, ntpServer2);

  // load habit data from eeprom

  display.init();
  display.flipScreenVertically();

  WiFi.begin(ssid, password);
  display.drawString(0, 0, "Initiating WiFi");
  display.display();
  while (WiFi.status() != WL_CONNECTED) { delay(10); }

  display.drawString(0, 12, "Initiating RTC");
  display.display();
  while (!getLocalTime(&timeInfo)) { delay(10); }
  
  displayHabitData();
}

void loop() {
  checkTime();
  checkButton();
  serialInHabitData();

  // Handle habit tracked!
  int month = timeInfo.tm_mon;
  int day = timeInfo.tm_mday - 1;
  if (buttonState == HIGH && habitData[month][day] == false) {
    habitData[month][day] = true;
    displayHabitData();
  }

  digitalWrite(LED, buttonState); // test code
}

void displayHabitData() {
  display.clear();

  // Draw year data
  for (int month = 0; month < 12; month++) {
    for (int day = 0; day < 31; day++) {
      if (habitData[month][day]) {
        int x = month * (width + columnSpace) + border;
        int y = day * height + border;
        display.fillRect(x, y, width, height);
      }
    }
  }

  // Draw week data
  time_t presentTime = time(NULL); // Get timestamp of current time
  int x = 12 * (width + columnSpace) + border; // All week data is at same x
  for (int dayOfWeek = 0; dayOfWeek < 7; dayOfWeek++) {
    int y = dayOfWeek * (weekSize + weekSpace) + border;
    if (dayOfWeek < timeInfo.tm_wday) { // Days before today should be checked
      time_t pastTime = presentTime - ((timeInfo.tm_wday - dayOfWeek) * ONE_DAY); // Create timestamp of date to be checked
      struct tm pastTimeInfo;
      localtime_r(&pastTime, &pastTimeInfo); // Convert timestamp to calendar info
      // Check the status of that day, and draw a filled or empty box based on that
      bool habit = habitData[pastTimeInfo.tm_mon][pastTimeInfo.tm_mday - 1];
      habit ? display.fillRect(x, y, weekSize, weekSize) : display.drawRect(x, y, weekSize, weekSize);
    } else if (dayOfWeek == timeInfo.tm_wday) { // Today
      // Check the status of today, and draw a filled or empty box based on that
      bool habit = habitData[timeInfo.tm_mon][timeInfo.tm_mday - 1];
      habit ? display.fillRect(x, y, weekSize, weekSize) : display.drawRect(x, y, weekSize, weekSize);
    } else { // Days after today will always be empty
      display.drawRect(x, y, weekSize, weekSize);
    }
  }

  display.display();
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
    if (!getLocalTime(&timeInfo)){
      Serial.println("No time available (yet)");
      return;
    }
    // Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S");
    // serialOutHabitData();
  }
}

void serialOutHabitData() {
  for (int i = 0; i < 12; i++) {
    char monthString[32] = "";
    for (int j = 0; j < 31; j++) {
      strlcat(monthString, habitData[i][j] ? "1" : "0", 32);
    }
    Serial.println(monthString);
  }
  Serial.println("-------------------------------");
}

void serialInHabitData() {
  while (Serial.available() > 0) {
    int month = Serial.parseInt();
    int day = Serial.parseInt();
    if (Serial.read() == '\n') {
      if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        habitData[month-1][day-1] = !habitData[month-1][day-1];
        displayHabitData();
      } else if (month == -1 && day == 1) {
        serialOutHabitData();
      } else if (month == -1 && day == 0) {
        memset(habitData, 0, sizeof(habitData));
        displayHabitData();
      }
    }
  }
}
