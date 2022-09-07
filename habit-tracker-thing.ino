/*
  Board: DOIT ESP32 DEVKIT V1
  I2C Pins: D21 (SDA) / D22 (SCL)
  Screen API: https://github.com/ThingPulse/esp8266-oled-ssd1306
  Time Docs: https://github.com/Patapom/Arduino/blob/master/Libraries/AVR%20Libc/avr-libc-2.0.0/include/time.h

  Proposed screen layout is in screen-reference.png
*/

/*
    To Do:
  Valid time check on button input
  Make data available on web server
*/

#include <WiFi.h>
#include <time.h>
#include <sntp.h>
#include "WiFiSettings.h"
#include <Wire.h>
#include <SSD1306Wire.h>
#include <Preferences.h>

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK  "your-password"
#endif

#define LED 5
#define BUTTON 23
#define BRIGHTNESS 32
#define DEBOUNCE_DELAY 50
#define TIME_CHECK_FREQUENCY 1000
#define ONE_DAY 86400

SSD1306Wire display(0x3c, SDA, SCL);
Preferences preferences;

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

const int oledMaxShift = 10; // Maximum number of pixels to shift display
const int oledShiftPeriod = 60000; // How long between shifts (in ms)
unsigned long oledLastShiftTime = 0; // Time of last shift
bool oledShiftDirection = true;  // Which direction we're currently shifting (true is right)
int oledShiftAmount = 0; // Current shift amount

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

  display.init();
  display.setBrightness(BRIGHTNESS);
  display.flipScreenVertically();

  WiFi.begin(ssid, password);
  display.drawString(0, 0, "Initiating WiFi");
  display.display();
  while (WiFi.status() != WL_CONNECTED) { delay(10); }

  display.drawString(0, 12, "Initiating RTC");
  display.display();
  while (!getLocalTime(&timeInfo)) { delay(10); }

  display.drawString(0, 12, "Loading data");
  display.display();
  if (!preferences.begin("habitData")) {
    display.drawString(0, 24, "ERROR LOADING DATA");
    display.display();
    delay(300000);
  }
  loadHabitData();
  
  displayHabitData();
}

void loop() {
  checkTime();
  checkButton();
  oledPixelShiftUpdate();
  serialInHabitData();

  // Handle habit tracked!
  int month = timeInfo.tm_mon;
  int day = timeInfo.tm_mday - 1;
  if (buttonState == HIGH && habitData[month][day] == false) {
    updateHabitData(month, day, true);
  }

  digitalWrite(LED, buttonState); // test code
}

// Draws habit data to the display
void displayHabitData() {
  display.clear();

  // Draw year data
  for (int month = 0; month < 12; month++) {
    for (int day = 0; day < 31; day++) {
      if (habitData[month][day]) {
        int x = month * (width + columnSpace) + border + oledShiftAmount;
        int y = day * height + border;
        display.fillRect(x, y, width, height);
      }
    }
  }

  // Draw week data
  time_t presentTime = time(NULL); // Get timestamp of current time
  int x = 12 * (width + columnSpace) + border + oledShiftAmount; // All week data is at same x
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

// Debounces button input
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

// Updates the RTC
void checkTime() {
  if ((millis() - lastTimeCheck) > TIME_CHECK_FREQUENCY) {
    lastTimeCheck = millis();
    getLocalTime(&timeInfo);
  }
}

// Periodically shifts pixels to save the OLED screen
void oledPixelShiftUpdate() {
  if ((millis() - oledLastShiftTime) > oledShiftPeriod) {
    oledLastShiftTime = millis();
    oledShiftAmount += oledShiftDirection ? 1 : -1;
    if (oledShiftAmount >= oledMaxShift || oledShiftAmount <= 0) {
      oledShiftDirection = !oledShiftDirection;
    }
  }
  displayHabitData();
}

// Use this function to make changes to habit data
// Only this function and loadHabitData should modify the habitData object
void updateHabitData(int month, int day, bool status) {
  habitData[month][day] = status;
  saveHabitData(month, day, status);
  displayHabitData();
}

//
// Preferences functions
//

// Loads habitData from Preferences
void loadHabitData() {
  char key[6];
  for (int month = 0; month < 12; month++) {
    for (int day = 0; day < 31; day++) {
      snprintf(key, 6, "%d-%d", month, day);
      habitData[month][day] = preferences.getBool(key);
    }
  }
}

// Saves habitData from Preferences
void saveHabitData(int month, int day, bool status) {
  char key[6];
  snprintf(key, 6, "%d-%d", month, day);
  preferences.putBool(key, status);
}

//
// Test functions
//

// Test function - checks habitData variable against Preferences
bool checkHabitData() {
  char key[6];
  for (int month = 0; month < 12; month++) {
    for (int day = 0; day < 31; day++) {
      snprintf(key, 6, "%d-%d", month, day);
      if (habitData[month][day] != preferences.getBool(key)) {
        return false;
      }
    }
  }
  return true;
}

// Test function - outputs habitData over serial
void serialOutHabitData() {
  for (int month = 0; month < 12; month++) {
    char monthString[32] = "";
    for (int day = 0; day < 31; day++) {
      strlcat(monthString, habitData[month][day] ? "1" : "0", 32);
    }
    Serial.println(monthString);
  }
  Serial.println("-------------------------------");
}

// Test function - allows modifying habitData over serial
void serialInHabitData() {
  while (Serial.available() > 0) {
    int month = Serial.parseInt();
    int day = Serial.parseInt();
    if (Serial.read() == '\n') {
      if (month >= 1 && month <= 12 && day >= 1 && day <= 31) { // m d : Toggle habit data for given day
        updateHabitData(month-1, day-1, !habitData[month-1][day-1]);
      } else if (month == -1 && day == 1) { // -1 1 : Output habit data over serial
        serialOutHabitData();
      } else if (month == -1 && day == 2) { // -1 2 : Check habitData variable against Preferences
        Serial.println("Checking habit data...");
        Serial.println(checkHabitData() ? "Passed" : "Failed");
      } else if (month == -1 && day == 0) { // -1 0 : Clears all data (habitData and Preferences)
        memset(habitData, 0, sizeof(habitData));
        preferences.clear();
        displayHabitData();
      }
    }
  }
}
