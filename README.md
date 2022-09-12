![Alt text](images/complete.jpg?raw=true "a photo of the complete habit tracker thing")

# habit tracker thing
A daily habit tracker built on the ESP32. If you do the thing you want to make a habit of, you get to push the button!

Pushing the button makes a little line representing the day show up on the screen. The screen shows 12 columns, each representing a month, making up a full year. The current week (Sun to Sat) is also displayed. See `screen-reference.png` to see the layout.

Optionally, the button can be made active only within a certain period so if the habit you want is "complete [your habit] before [a specific time]" you have to hit the button before that time. While the button is active the green LED is lit.

The habit tracker thing is connected to WiFi for the time. WiFi credentials can be added in the main `habit-tracker-thing.ino` file or in `WiFiSettings.h`.

To build the habit tracker thing you'll need an ESP32, a SSD1306 display, a button, an LED, and a resistor. To fully complete the project you'll also need some soldering skills and a 3D printer. Files for the enclosure are available in `3mf/`.