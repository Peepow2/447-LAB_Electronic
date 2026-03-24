#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi Credentials
const char* ssid     = "SSID";
const char* password = "PASS";

// NTP Time settings (GMT+7 for Thailand)
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 7;
const int daylightOffset_sec = 0;

// OLED Display Settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Push Button Settings
#define BUTTON_PIN 27  

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool showIP = false; // Toggle flag for switching screen

unsigned long lastDebounceTime = 0;
const int debounceDelay = 250; // ป้องกันการกดซ้ำเร็วเกินไป

int lastButtonState = HIGH
int buttonState;

String month[] = {"X", "January", "Febuary", "March", "April", "May", \
                  "June", "July", "August", "September", "October", \
                  "November", "December"}

void setup() 
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Connect to WiFi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
  {
    Serial.println("SSD1306 allocation failed");
    while (true);
  }

  display.clearDisplay();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() 
{
  // Button push detect
  buttonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && buttonState == LOW && millis() - lastDebounceTime > debounceDelay) 
  {
    showIP = !showIP;
    lastDebounceTime = millis();
    Serial.println(F("Button Pressed!"));
  }

  lastButtonState = buttonState;  // save state

  // OLED Update
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (showIP) 
  {
    Serial.println(F("BUTTON_PIN == switch1"));
    // Show IP Address
    display.setTextSize(1);
    display.println("ESP32 IP is");
    display.println(WiFi.localIP());
    display.setTextSize(1);
    display.println("[ Group 3 ]")); 
  } 

  else 
  {
    Serial.println("BUTTON_PIN == switch2");
    // Show Time & Date
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) 
    {
      display.setTextSize(1);
      char timeString[9];
      strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
      display.println(timeString); // แสดงเวลา HH:MM:SS

      char date_show[15];
      strftime(date_show, sizeof(date_show), "%d/%B/%Y", &timeinfo);
      display.println(date_show);
    } 
    
    else 
    {
      display.println("Time Error");
    }
    display.setTextSize(1);
    display.println("[ Group 3 ]")); 
  }
  display.display();
}
