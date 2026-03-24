#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup()
{
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
  {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
}

void loop() 
{
  display.clearDisplay();
  display.setTextSize(1);               // set Text size
  display.setTextColor(SSD1306_WHITE);  // set text color
  display.setCursor(0, 0);              // set Cursor
  display.println("IoT 2102447");
  display.println("[ Group 03 ]");
  display.println("CUEE 2-2025");
  display.display();                    // show
  delay(1000);                          // stop for 1 sec

  display.clearDisplay();
  display.setTextSize(1);               // set Text size
  display.setTextColor(SSD1306_WHITE);  // set text color
  display.setCursor(0, 0);              // set Cursor
  display.println("OLED Testing");
  display.println("IOT Week1");
  display.println("[ Group 03 ]");
  display.display();                    // show
  delay(1000);                          // stop for 1 sec 
}
