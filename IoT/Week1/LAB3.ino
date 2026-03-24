#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int PWM_CHANNEL = 0;
const int PWM_FREQ = 500;
const int PWM_RESOLUTION = 8;
const int MAX_DUTY_CYCLE = (int)(pow(2, PWM_RESOLUTION) - 1);

const int LED_1_OUTPUT_PIN = 2; // Red
const int LED_2_OUTPUT_PIN = 0; //Yellow
const int LED_3_OUTPUT_PIN = 4; //Green

const int BUTTON_K1 = 27;  // Control RED LED
const int BUTTON_K2 = 26;  // Control YELLOW LED
const int BUTTON_K3 = 25;  // Control GREEN LED
const int BUTTON_K4 = 33;  // Control ALL LEDs

const int POT_PIN = 34;

int Switch_State = 0;

void setup() 
{
    Serial.begin(115200);

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
    }

    // Clear display buffer
    display.clearDisplay();

    // Set GPIO for LED
    ledcAttachChannel(LED_1_OUTPUT_PIN, PWM_FREQ, PWM_RESOLUTION, 1);
    ledcAttachChannel(LED_2_OUTPUT_PIN, PWM_FREQ, PWM_RESOLUTION, 2);
    ledcAttachChannel(LED_3_OUTPUT_PIN, PWM_FREQ, PWM_RESOLUTION, 3);

    // Set button pin mode
    pinMode(BUTTON_K1, INPUT_PULLUP);
    pinMode(BUTTON_K2, INPUT_PULLUP);
    pinMode(BUTTON_K3, INPUT_PULLUP);
    pinMode(BUTTON_K4, INPUT_PULLUP);

}


void loop() 
{
    int dutyCycle = analogRead(POT_PIN);
    dutyCycle = map(dutyCycle, 0, 4095, 0, MAX_DUTY_CYCLE);
    
    bool k1Pressed = digitalRead(BUTTON_K1);
    bool k2Pressed = digitalRead(BUTTON_K2);
    bool k3Pressed = digitalRead(BUTTON_K3);
    bool k4Pressed = digitalRead(BUTTON_K4);

    Serial.print("K1: "); Serial.print(k1Pressed);
    Serial.print(" | K2: "); Serial.print(k2Pressed);
    Serial.print(" | K3: "); Serial.print(k3Pressed);
    Serial.print(" | K4: "); Serial.println(k4Pressed);

    if (k1Pressed == 0)
      Switch_State = 1;
    else if (k2Pressed == 0)
      Switch_State = 2;
    else if (k3Pressed == 0)
      Switch_State = 3;
    else if (k4Pressed == 0)
      Switch_State = 4;

    Serial.print("Switch_State: "); Serial.println(Switch_State);

    // Control LEDs based on button presses
    if (Switch_State == 1) 
    {
        ledcWriteChannel(1, dutyCycle);
        ledcWriteChannel(2, 0);
        ledcWriteChannel(3, 0);
    } 
    else if (Switch_State == 2) 
    {
        ledcWriteChannel(1, 0);
        ledcWriteChannel(2, dutyCycle);
        ledcWriteChannel(3, 0);
    } 
    else if (Switch_State == 3) {
        ledcWriteChannel(1, 0);
        ledcWriteChannel(2, 0);
        ledcWriteChannel(3, dutyCycle);
    }
    else 
    {
        ledcWriteChannel(1, dutyCycle);
        ledcWriteChannel(2, dutyCycle);
        ledcWriteChannel(3, dutyCycle);
    }
    delay(100);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("PWM Duty Cycle"));
    display.print("Value is "); 
    display.println(dutyCycle);
    display.println("[ Group 03 ]");
    display.display();
    delay(100); 
}
