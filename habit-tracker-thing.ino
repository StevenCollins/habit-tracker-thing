/*
  Board: DOIT ESP32 DEVKIT V1
  I2C Pins: D21 (SDA) / D22 (SCL)
  Screen API: https://github.com/ThingPulse/esp8266-oled-ssd1306
  Time Docs: https://github.com/Patapom/Arduino/blob/master/Libraries/AVR%20Libc/avr-libc-2.0.0/include/time.h
  Screen layout: ./screen-reference.png
*/

/*
    To Do:
  Make data available on web server
  Allow updating valid time on web server
  Allow inputting the habit to be tracked and other notes through the webserver
  Store valid time in preferences
  Make end of month visible on "calendar" using (animated?) background
    Unchecked days will cover background making month ends visible
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

const int oledBrightness = 32; // Brightness (0-255)
const int oledMaxShift = 10; // Maximum number of pixels to shift display
const int oledShiftPeriod = 60000; // How long between shifts (in ms)
unsigned long oledLastShiftTime = 0; // Time of last shift
bool oledShiftDirection = true;  // Which direction we're currently shifting (true is right)
int oledShiftAmount = 0; // Current shift amount
bool oledShiftY = false; // Shifts Y by 1

const int debounceDelay = 50;
unsigned long lastDebounceTime = 0;
int lastButtonState = LOW;
int buttonState;
bool buttonActive = true;

const int timeCheckPeriod = 1000;
unsigned long lastTimeCheck = 0;
struct tm timeInfo;
int validTimeStartHour = 6;
int validTimeStartMinute = 0;
int validTimeEndHour = 9;
int validTimeEndMinute = 30;
int validTimeStart() { return validTimeStartHour * 60 + validTimeStartMinute; } // Start time in minutes past midnight
int validTimeEnd() { return validTimeEndHour * 60 + validTimeEndMinute; } // End time in minutes past midnight

bool habitData[12][31];

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, buttonActive); // LED and button status must start the same

  configTzTime(timeZone, ntpServer1, ntpServer2);

  display.init();
  display.setBrightness(oledBrightness);
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
  updateRTC();
  updateButtonStatus();
  checkButton();
  oledPixelShiftUpdate();
  serialInHabitData();

  // Handle habit tracked!
  int month = timeInfo.tm_mon;
  int day = timeInfo.tm_mday - 1;
  if (buttonState == HIGH && buttonActive) {
    updateHabitData(month, day, true);
  }
}

// Draws habit data to the display
void displayHabitData() {
  display.clear();

  // Draw year data
  for (int month = 0; month < 12; month++) {
    for (int day = 0; day < 31; day++) {
      if (month == timeInfo.tm_mon && day == timeInfo.tm_mday - 1) {
        int x = month * (width + columnSpace) + border + oledShiftAmount;
        int y = day * height + border + (oledShiftY ? 1 : 0);
        if (habitData[month][day]) {
          display.setPixel(x, y);
          display.setPixel(x + 2, y);
          display.setPixel(x + 3, y);
          display.setPixel(x + 5, y);
        } else {
          display.setPixel(x + 1, y);
          display.setPixel(x + 4, y);
        }
      } else if (habitData[month][day]) {
        int x = month * (width + columnSpace) + border + oledShiftAmount;
        int y = day * height + border + (oledShiftY ? 1 : 0);
        display.fillRect(x, y, width, height - 1);
      }
    }
  }

  // Draw week data
  time_t presentTime = time(NULL); // Get timestamp of current time
  int x = 12 * (width + columnSpace) + border + oledShiftAmount; // All week data is at same x
  for (int dayOfWeek = 0; dayOfWeek < 7; dayOfWeek++) {
    int y = dayOfWeek * (weekSize + weekSpace) + border + (oledShiftY ? 1 : 0);
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
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (buttonReading != buttonState) {
        buttonState = buttonReading;
    }
  }
  lastButtonState = buttonReading;
}

// Updates the RTC
void updateRTC() {
  if ((millis() - lastTimeCheck) > timeCheckPeriod) {
    lastTimeCheck = millis();
    getLocalTime(&timeInfo);
  }
}

// Activiates or deactivates the button based on current status, time, and habit data
void updateButtonStatus() {
  int currentMinutesPastMidnight = timeInfo.tm_hour * 60 + timeInfo.tm_min;
  bool timeValid = currentMinutesPastMidnight >= validTimeStart() && currentMinutesPastMidnight <= validTimeEnd();

  if (!buttonActive && !habitData[timeInfo.tm_mon][timeInfo.tm_mday - 1] && timeValid) {
    // Button is not active and should be (habit not tracked and valid time)
    buttonActive = true;
    digitalWrite(LED, HIGH);
  } else if (buttonActive && (habitData[timeInfo.tm_mon][timeInfo.tm_mday - 1] || !timeValid)) {
    // Button is active and should not be (habit tracked or invalid time)
    buttonActive = false;
    digitalWrite(LED, LOW);
  }
}

// Periodically shifts pixels to save the OLED screen
void oledPixelShiftUpdate() {
  if ((millis() - oledLastShiftTime) > oledShiftPeriod) {
    oledLastShiftTime = millis();
    oledShiftAmount += oledShiftDirection ? 1 : -1;
    if (oledShiftAmount >= oledMaxShift) {
      oledShiftDirection = !oledShiftDirection;
    } else if (oledShiftAmount <= 0) {
      oledShiftDirection = !oledShiftDirection;
      oledShiftY = !oledShiftY;
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
      } else if (month == -1 && day == 3) { // -1 3 : Output current IP address and time
        Serial.println(WiFi.localIP());
        Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S");
        Serial.print("Start/End/Current MPM: ");
        Serial.print(validTimeStart());
        Serial.print("/");
        Serial.print(validTimeEnd());
        Serial.print("/");
        Serial.println(timeInfo.tm_hour * 60 + timeInfo.tm_min);
      } else if (month == -1 && day == 0) { // -1 0 : Clears all data (habitData and Preferences)
        memset(habitData, 0, sizeof(habitData));
        preferences.clear();
        displayHabitData();
      }
    }
  }
}
