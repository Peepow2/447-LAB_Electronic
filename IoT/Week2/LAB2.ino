#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>  // for SSD1306
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>  // for BME280
#include <WiFi.h>  // for ESP32 WiFi
#include <PubSubClient.h>  // for NETPIE
#include "credentials.h"  // WiFi and NETPIE credentials
#include "iot_iconset_16x16.h"

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1  // OLED reset pin (or -1 if sharing Arduino reset pin)
#define OLED_ADDRESS  0x3C  // OLED I2C address (Adafruit = 0x3D China 0x3C)
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  // OLED
#define SEALEVELPRESSURE_HPA (1013.25)
#define BME_ADDRESS 0x76  // BME I2C address (Red module = 0x77 Purple module = 0x76)
Adafruit_BME280 bme;  // BME
WiFiClient espClient;
const char* mqtt_server = "broker.netpie.io";
const int mqtt_port = 1883;
PubSubClient client(espClient);

float pressure,altitude,temperature,humidity;
char msg[100];

void setup() {
  Serial.begin(115200);
  delay(100);

  while(!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("OLED failed to begin"));
    delay(100);
  }
  oled.clearDisplay(); // clears the screen and buffer
  oled.setTextSize(1);
  oled.setTextColor(WHITE, BLACK);
  oled.setCursor(0,0);
  oled.println(F("OLED passed"));
  oled.display();
  delay(100);

  while(!bme.begin(BME_ADDRESS)) {
    oled.setCursor(0,oled.getCursorY());
    oled.print(F("BME failed to begin"));
    oled.display();
    delay(100);
  }
  oled.setCursor(0,oled.getCursorY());
  oled.println(F("BME passed         "));
  oled.display();
  delay(100);

  WiFi.begin(ssid, password);
  delay(100);

  client.setServer(mqtt_server, mqtt_port);
  client.connect(mqtt_client, mqtt_username, mqtt_password);
  Serial.println(F("NETPIE CONNECTED"));
  delay(100);

  // Task creation for FreeRTOS multitasking
  xTaskCreate(vTaskPressureAltitude, "PressureAltitude", 4096, NULL, 1, NULL);
  xTaskCreate(vTaskTemperature, "Temperature", 4096, NULL, 1, NULL);
  xTaskCreate(vTaskHumidity, "Humidity", 4096, NULL, 1, NULL);
  xTaskCreate(vTaskConnectionCheck, "ConnectionCheck", 4096, NULL, 1, NULL);
  xTaskCreate(vTaskOLED, "OLED", 8192, NULL, 1, NULL);
}

void loop() {
  // use FreeRTOS
}

// Task 1: Read pressure and altitude, send to NETPIE every 3 seconds
void vTaskPressureAltitude(void * pvParameters) {
  for (;;) {
    pressure = bme.readPressure() / 100.0f;  // in hPa
    altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);  // in meters
    if (client.connected()) {
      String payload = "{\"data\": {\"pressure\":" + String(pressure, 2) + ", \"altitude\": 0}}";
      payload.toCharArray(msg, (payload.length() + 1));
      client.publish("@shadow/data/update", msg);
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS);  // 3 seconds
  }
}

// Task 2: Read temperature, send to NETPIE every 4 seconds
void vTaskTemperature(void * pvParameters) {
  for (;;) {
    temperature = bme.readTemperature();  // in Celsius
    if (client.connected()) {
      String payload = "{\"data\": {\"temperature\":" + String(temperature, 2) + "}}";
      payload.toCharArray(msg, (payload.length() + 1));
      client.publish("@shadow/data/update", msg);
    }
    vTaskDelay(4000 / portTICK_PERIOD_MS);  // 4 seconds
  }
}

// Task 3: Read humidity, send to NETPIE every 5 seconds
void vTaskHumidity(void * pvParameters) {
  for (;;) {
    humidity = bme.readHumidity();  // in %RH
    if (client.connected()) {
      String payload = "{\"data\": {\"humidity\":" + String(humidity, 2) + "}}";
      payload.toCharArray(msg, (payload.length() + 1));
      client.publish("@shadow/data/update", msg);
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);  // 5 seconds
  }
}

// Task 4: Check WiFi and NETPIE connection every 7 seconds
void vTaskConnectionCheck(void * pvParameters) {
  for (;;) {
  if(client.connected()) {
    client.loop();
    client.publish("@shadow/data/update", msg);

  } else {
    if(WiFi.status() == WL_CONNECTED) {
      client.disconnect();
      client.connect(mqtt_client, mqtt_username, mqtt_password);
    } else {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
    vTaskDelay(7000 / portTICK_PERIOD_MS);  // 7 seconds
  }
}

// Task 5: Display data on OLED every 1 second
void vTaskOLED(void * pvParameters) {
  for (;;) {
    oled.clearDisplay();
    oled.setTextSize(1);

    // Display Pressure with icon
    oled.drawBitmap(0, 0, timer_icon16x16, 16, 16, 1);  // Pressure icon
    oled.setCursor(20, 0);
    oled.print("Pres:");
    oled.println(String(pressure, 2) + " hPa");

    // Display Altitude with icon
    oled.drawBitmap(0, 16, altitude_icon16x16, 16, 16, 1);  // Altitude icon
    oled.setCursor(20, 16);
    oled.print("Alt: ");
    oled.println(String(altitude, 2) + " m");

    // Display Temperature with icon
    oled.drawBitmap(0, 32, temperature_icon16x16, 16, 16, 1);  // Temperature icon
    oled.setCursor(20, 32);
    oled.print("Temp:");
    oled.println(String(temperature, 2) + " C");

    // Display Humidity with icon
    oled.drawBitmap(0, 48, humidity2_icon16x16, 16, 16, 1);  // Humidity icon
    oled.setCursor(20, 48);
    oled.print("Hum: ");
    oled.println(String(humidity, 2) + " %Rh");

    // Display WiFi Status and Icon
    if (WiFi.status() == WL_CONNECTED) {
      oled.drawBitmap(112, 48, wifi1_icon16x16, 16, 16, 1);  // WiFi icon for connected status
      oled.setCursor(64, 56);
      oled.println("WiFi: OK");
    } else {
      oled.setCursor(64, 56);
      oled.println("WiFi: ...");
    }

    oled.display();
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Update every 1 second
  }
}
