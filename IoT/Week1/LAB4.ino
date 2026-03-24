#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin Definitions
const int buttonPin1 = 27;  // K1 Button
const int buttonPin2 = 26;  // K2 Button
const int buttonPin3 = 25;  // K3 Button
const int buttonPin4 = 33;  // K4 Button

// OLED Display Settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 200;  // กันกดซ้ำ

bool lastButtonState1 = HIGH;
bool lastButtonState2 = HIGH;
bool lastButtonState3 = HIGH;
bool lastButtonState4 = HIGH; // สถานะปุ่มก่อนหน้า
bool buttonState1, buttonState2, buttonState3, buttonState4; // สถานะปุ่มปัจจุบัน

void setup() 
{
  Serial.begin(115200);

  pinMode(buttonPin1, INPUT_PULLUP);
  pinMode(buttonPin2, INPUT_PULLUP);
  pinMode(buttonPin3, INPUT_PULLUP);
  pinMode(buttonPin4, INPUT_PULLUP);

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
  {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Ready!"));
  display.display();
}


void show(string Message)
{

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(Message);
    display.display();
    lastDebounceTime = millis();
}

void loop() 
{
  bool reading1 = digitalRead(buttonPin1);
  bool reading2 = digitalRead(buttonPin2);
  bool reading3 = digitalRead(buttonPin3);
  bool reading4 = digitalRead(buttonPin4);

  Serial.println("..............");
  Serial.println(reading1);
  Serial.println(reading2);
  Serial.println(reading3);
  Serial.println(reading4);
  
  if (reading1 == LOW && lastButtonState1 == HIGH && \
        (millis() - lastDebounceTime) > debounceDelay) 
  {
    show("IoT 2102447");
    lastDebounceTime = millis();
  }

  if (reading2 == LOW && lastButtonState2 == HIGH && \
        (millis() - lastDebounceTime) > debounceDelay) 
  {
    show("Electrical CU");
    lastDebounceTime = millis();
  }

  if (reading3 == LOW && lastButtonState3 == HIGH && \
        (millis() - lastDebounceTime) > debounceDelay) 
  {
    show("OLED is work!")
    lastDebounceTime = millis();
  }

  if (reading4 == LOW && lastButtonState4 == HIGH && \
        (millis() - lastDebounceTime) > debounceDelay) 
  {
    show("Tua Hom Mak")
    lastDebounceTime = millis();
  }
  
  lastButtonState1 = reading1;
  lastButtonState2 = reading2;
  lastButtonState3 = reading3;
  lastButtonState4 = reading4;
  delay(100);
}
