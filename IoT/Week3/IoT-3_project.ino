#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_BME280.h>
#include <Adafruit_TSL2561_U.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "credentials.h"

// ── Pin definitions ───────────────────────────────────────────────
#define PIN_PIR          14
#define PIN_SW1          25
#define PIN_SW2          26
#define PIN_SW3          27
#define PIN_SW4          32
#define PIN_POT          34
#define PIN_LED_RED      13
#define PIN_LED_YELLOW    2
#define PIN_LED_GREEN    15
#define PIN_DS18B20       4

// ── OLED ──────────────────────────────────────────────────────────
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64
#define OLED_RESET       -1
#define OLED_ADDRESS   0x3C

// ── BME280 ────────────────────────────────────────────────────────
#define BME_ADDRESS    0x76

// ── NETPIE ────────────────────────────────────────────────────────
const char* mqtt_server  = "broker.netpie.io";
const int   mqtt_port    = 1883;
#define TOPIC_SHADOW_SET  "@shadow/data/update"
#define TOPIC_FEED        "@msg/ghost_event"

// ── FreeRTOS event bit ────────────────────────────────────────────
#define PIR_BIT  (1 << 0)

// ── Sensitivity modes ─────────────────────────────────────────────
typedef enum { MODE_GHOST = 0, MODE_NORMAL, MODE_STORM } SensorMode;
const char* modeNames[] = { "GHOST", "NORMAL", "STORM" };
const float deadBand[]  = { 1.0f, 2.0f, 4.0f };

// ── Structs ───────────────────────────────────────────────────────
typedef struct {
  float bme_temp;
  float humidity;
  float pressure;
  float lux;
  float ds_temp;
  unsigned long timestamp_ms;
} SensorPacket_t;

typedef struct {
  int        score;
  int        worst_factor;
  SensorMode mode;
  bool       alert;
} GhostScore_t;

// ── FreeRTOS handles ──────────────────────────────────────────────
SemaphoreHandle_t  i2cMutex;
SemaphoreHandle_t  baselineMutex;
SemaphoreHandle_t  mqttMutex;
QueueHandle_t      sensorQueue;
QueueHandle_t      latestSensorQueue;  // 1-slot latest-value cache
QueueHandle_t      scoreQueue;
EventGroupHandle_t ghostEvent;

// ── Baseline ──────────────────────────────────────────────────────
SensorPacket_t baseline;
float baseline_sigma[5] = { 0.3f, 1.0f, 0.5f, 20.0f, 0.3f };

// ── Hardware objects ──────────────────────────────────────────────
Adafruit_BME280          bme;
Adafruit_TSL2561_Unified tsl(TSL2561_ADDR_FLOAT, 12345);
OneWire                  oneWire(PIN_DS18B20);
DallasTemperature        ds(&oneWire);
Adafruit_SSD1306         oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClient               espClient;
PubSubClient             client(espClient);

// ── Volatile shared state ─────────────────────────────────────────
volatile bool       pirTriggered = false;
volatile SensorMode currentMode  = MODE_GHOST;

// ─────────────────────────────────────────────────────────────────
// PIR ISR
// ─────────────────────────────────────────────────────────────────
void IRAM_ATTR pirISR() {
  static unsigned long lastTrigger = 0;
  unsigned long now = millis();
  if (now - lastTrigger < 100) return;
  lastTrigger  = now;
  pirTriggered = true;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xEventGroupSetBitsFromISR(ghostEvent, PIR_BIT, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ─────────────────────────────────────────────────────────────────
// Helper: read all sensors
// ─────────────────────────────────────────────────────────────────
SensorPacket_t readSensors() {
  SensorPacket_t p;
  p.timestamp_ms = millis();
  xSemaphoreTake(i2cMutex, portMAX_DELAY);
  p.bme_temp = bme.readTemperature();
  p.humidity = bme.readHumidity();
  p.pressure = bme.readPressure() / 100.0f;
  sensors_event_t event;
  tsl.getEvent(&event);
  p.lux = event.light;
  xSemaphoreGive(i2cMutex);
  ds.requestTemperatures();
  p.ds_temp = ds.getTempCByIndex(0);
  return p;
}

// ─────────────────────────────────────────────────────────────────
// Task A — PIR monitor  (Priority 3)
// ─────────────────────────────────────────────────────────────────
void taskPIR(void* pvParam) {
  attachInterrupt(digitalPinToInterrupt(PIN_PIR), pirISR, RISING);
  uint8_t sw1prev = HIGH;
  while (true) {
    uint8_t sw1 = digitalRead(PIN_SW1);
    if (sw1 == LOW && sw1prev == HIGH) {
      vTaskDelay(pdMS_TO_TICKS(50));
      currentMode = (SensorMode)((currentMode + 1) % 3);
    }
    sw1prev = sw1;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ─────────────────────────────────────────────────────────────────
// Task B — Sensor burst  (Priority 2)
// ─────────────────────────────────────────────────────────────────
void taskSensorBurst(void* pvParam) {
  const TickType_t idlePeriod  = pdMS_TO_TICKS(10000);
  const TickType_t burstPeriod = pdMS_TO_TICKS(1000);
  const int        burstCount  = 30;
  while (true) {
    EventBits_t bits = xEventGroupWaitBits(
      ghostEvent, PIR_BIT, pdTRUE, pdFALSE, idlePeriod
    );
    if (bits & PIR_BIT) {
      for (int i = 0; i < burstCount; i++) {
        SensorPacket_t p = readSensors();
        xQueueSend(sensorQueue, &p, 0);
        xQueueOverwrite(latestSensorQueue, &p);
        vTaskDelay(burstPeriod);
      }
    } else {
      SensorPacket_t p = readSensors();
      xQueueSend(sensorQueue, &p, 0);
      xQueueOverwrite(latestSensorQueue, &p);
    }
  }
}

// ─────────────────────────────────────────────────────────────────
// Task C — Ghost score engine  (Priority 2)
// ─────────────────────────────────────────────────────────────────
void taskScoreEngine(void* pvParam) {
  while (true) {
    SensorPacket_t p;
    if (xQueueReceive(sensorQueue, &p, portMAX_DELAY) != pdTRUE) continue;
    float pot = analogRead(PIN_POT) / 4095.0f;
    float w[5];
    w[0] = 0.30f - pot * 0.10f;
    w[1] = 0.20f - pot * 0.05f;
    w[2] = 0.20f + pot * 0.10f;
    w[3] = 0.15f + pot * 0.15f;
    w[4] = 0.15f - pot * 0.10f;
    bool swEnable[5] = {
      true,
      true,
      (digitalRead(PIN_SW3) == HIGH),
      (digitalRead(PIN_SW4) == HIGH),
      (digitalRead(PIN_SW2) == HIGH)
    };
    xSemaphoreTake(baselineMutex, portMAX_DELAY);
    float deltas[5] = {
      fabsf(p.bme_temp - baseline.bme_temp),
      fabsf(p.humidity - baseline.humidity),
      fabsf(p.pressure - baseline.pressure),
      fabsf(p.lux      - baseline.lux),
      fabsf(p.ds_temp  - baseline.ds_temp)
    };
    xSemaphoreGive(baselineMutex);
    float db       = deadBand[(int)currentMode];
    float rawScore = 0.0f;
    int   worstIdx = 0;
    float worstVal = 0.0f;
    for (int i = 0; i < 5; i++) {
      if (!swEnable[i]) continue;
      float norm = (deltas[i] / baseline_sigma[i]) * w[i];
      if (norm > worstVal) { worstVal = norm; worstIdx = i; }
      rawScore += norm;
    }
    int score = (int)constrain((rawScore / db) * 20.0f, 0, 100);
    GhostScore_t gs;
    gs.score        = score;
    gs.worst_factor = worstIdx;
    gs.mode         = currentMode;
    gs.alert        = (score >= 50);
    xQueueOverwrite(scoreQueue, &gs);
  }
}

// ─────────────────────────────────────────────────────────────────
// Task D — OLED + LED display  (Priority 1)
// ─────────────────────────────────────────────────────────────────
void taskDisplay(void* pvParam) {
  const char* factorLabels[] = {
    "TEMP BME", "HUMIDITY", "PRESSURE", "LUX", "TEMP DS"
  };
  GhostScore_t gs = { 0, 0, MODE_GHOST, false };
  while (true) {
    xQueuePeek(scoreQueue, &gs, 0);
    int score = gs.score;
    // LED traffic light
    if (score <= 30) {
      ledcWrite(PIN_LED_RED,    0);
      ledcWrite(PIN_LED_YELLOW, 0);
      ledcWrite(PIN_LED_GREEN,  200);
    } else if (score <= 60) {
      ledcWrite(PIN_LED_RED,    0);
      ledcWrite(PIN_LED_YELLOW, 200);
      ledcWrite(PIN_LED_GREEN,  0);
    } else {
      ledcWrite(PIN_LED_GREEN,  0);
      ledcWrite(PIN_LED_YELLOW, 0);
      ledcWrite(PIN_LED_RED,    (int)map(score, 61, 100, 80, 255));
    }
    // OLED
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("GHOST DETECTOR");
    oled.setCursor(0, 10);
    oled.print("Score:");
    oled.setTextSize(2);
    oled.setCursor(48, 8);
    oled.print(score);
    oled.setTextSize(1);
    oled.setCursor(0, 30);
    oled.print("Worst:");
    oled.print(factorLabels[gs.worst_factor]);
    oled.setCursor(0, 42);
    oled.print("Mode: ");
    oled.print(modeNames[gs.mode]);
    oled.setCursor(0, 54);
    if (gs.alert) {
      oled.print("!! ALERT !!");
    } else {
      oled.print("   clear   ");
    }
    oled.display();
    xSemaphoreGive(i2cMutex);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ─────────────────────────────────────────────────────────────────
// Task E — NETPIE uplink  (Priority 1)
// ─────────────────────────────────────────────────────────────────
void taskNetpie(void* pvParam) {
  int           eventCount    = 0;
  int           maxScoreToday = 0;
  unsigned long lastPublish   = 0;
  unsigned long lastFeedPub   = 0;
  const unsigned long FEED_INTERVAL_MS = 5000;   // publish feed every 5 s
  while (true) {
    unsigned long now = millis();

    // ── WiFi reconnect ────────────────────────────────────────
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected - reconnecting...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    // ── MQTT reconnect ────────────────────────────────────────
    if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE) {
      if (!client.connected()) {
        Serial.println("MQTT disconnected - reconnecting...");
        if (client.connect(mqtt_client, mqtt_username, mqtt_password)) {
          Serial.println("MQTT connected");
        } else {
          Serial.print("MQTT failed rc=");
          Serial.println(client.state());
          xSemaphoreGive(mqttMutex);
          vTaskDelay(pdMS_TO_TICKS(3000));
          continue;
        }
      }
      client.loop();
      xSemaphoreGive(mqttMutex);
    }

    // ── Read latest score and sensors from queues (no I2C here) ──
    GhostScore_t gs = { 0, 0, MODE_GHOST, false };
    xQueuePeek(scoreQueue, &gs, 0);
    if (gs.score > maxScoreToday) maxScoreToday = gs.score;

    SensorPacket_t p = {0, 0, 0, 0, 0, 0};
    xQueuePeek(latestSensorQueue, &p, 0);

    // ── Feed publish: on PIR OR every FEED_INTERVAL_MS ────────
    bool feedDueToTimer = (now - lastFeedPub >= FEED_INTERVAL_MS);
    bool feedDueToPIR   = pirTriggered;
    if (feedDueToPIR || feedDueToTimer) {
      if (feedDueToPIR) pirTriggered = false;
      lastFeedPub = now;
      eventCount++;
      String feedPayload = "{\"data\": {";
      feedPayload += "\"event_id\":"     + String(eventCount);
      feedPayload += ",\"trigger\":\""   + String(feedDueToPIR ? "pir" : "timer") + "\"";
      feedPayload += ",\"ghost_score\":" + String(gs.score);
      feedPayload += ",\"worst\":"       + String(gs.worst_factor);
      feedPayload += ",\"mode\":\""      + String(modeNames[gs.mode]) + "\"";
      feedPayload += ",\"bme_temp\":"    + String(p.bme_temp,  2);
      feedPayload += ",\"humidity\":"    + String(p.humidity,  2);
      feedPayload += ",\"pressure\":"    + String(p.pressure,  2);
      feedPayload += ",\"lux\":"         + String(p.lux,       2);
      feedPayload += ",\"ds_temp\":"     + String(p.ds_temp,   2);
      feedPayload += "}}";
      char feedBuf[512];
      feedPayload.toCharArray(feedBuf, feedPayload.length() + 1);

      if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE) {
        bool ok = client.publish(TOPIC_FEED, feedBuf);
        xSemaphoreGive(mqttMutex);
        Serial.printf("Feed publish %s (state=%d): %s\n",
                      ok ? "OK" : "FAIL", client.state(), feedBuf);
      }
    }

    // ── Shadow publish every 5 s ──────────────────────────────
    if (now - lastPublish >= 5000) {
      lastPublish = now;
      xSemaphoreTake(baselineMutex, portMAX_DELAY);
      float bl_temp = baseline.bme_temp;
      float bl_hum  = baseline.humidity;
      float bl_pres = baseline.pressure;
      float bl_lux  = baseline.lux;
      float bl_ds   = baseline.ds_temp;
      xSemaphoreGive(baselineMutex);
      String payload = "{\"data\": {";
      payload += "\"device_mode\":\""      + String(modeNames[gs.mode]) + "\"";
      payload += ",\"ghost_score\":"       + String(gs.score);
      payload += ",\"alert_active\":"      + String(gs.alert ? "true" : "false");
      payload += ",\"bme_temp_c\":"        + String(p.bme_temp,  2);
      payload += ",\"bme_humidity_pct\":"  + String(p.humidity,  2);
      payload += ",\"bme_pressure_hpa\":"  + String(p.pressure,  2);
      payload += ",\"tsl_lux\":"           + String(p.lux,       2);
      payload += ",\"ds18b20_temp_c\":"    + String(p.ds_temp,   2);
      payload += ",\"baseline_temp\":"     + String(bl_temp,     2);
      payload += ",\"baseline_humidity\":" + String(bl_hum,      2);
      payload += ",\"baseline_pressure\":" + String(bl_pres,     2);
      payload += ",\"baseline_lux\":"      + String(bl_lux,      2);
      payload += ",\"baseline_ds\":"       + String(bl_ds,       2);
      payload += ",\"events_today\":"      + String(eventCount);
      payload += ",\"max_score_today\":"   + String(maxScoreToday);
      payload += "}}";
      char msg[1024];
      payload.toCharArray(msg, payload.length() + 1);

      if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE) {
        bool ok = client.publish(TOPIC_SHADOW_SET, msg);
        xSemaphoreGive(mqttMutex);
        Serial.printf("Shadow publish %s (state=%d, len=%d)\n",
                      ok ? "OK" : "FAIL", client.state(), (int)payload.length());
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ─────────────────────────────────────────────────────────────────
// Baseline calibration
// ─────────────────────────────────────────────────────────────────
void calibrateBaseline() {
  Serial.println("Calibrating baseline (60 s) - keep room still...");
  xSemaphoreTake(i2cMutex, portMAX_DELAY);
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);  oled.print("Calibrating...");
  oled.setCursor(0, 12); oled.print("Keep room still");
  oled.display();
  xSemaphoreGive(i2cMutex);
  const int N = 60;
  float sumT=0, sumH=0, sumP=0, sumL=0, sumD=0;
  float sumT2=0, sumH2=0, sumP2=0, sumL2=0, sumD2=0;
  for (int i = 0; i < N; i++) {
    SensorPacket_t p = readSensors();
    sumT += p.bme_temp;  sumT2 += p.bme_temp  * p.bme_temp;
    sumH += p.humidity;  sumH2 += p.humidity  * p.humidity;
    sumP += p.pressure;  sumP2 += p.pressure  * p.pressure;
    sumL += p.lux;       sumL2 += p.lux       * p.lux;
    sumD += p.ds_temp;   sumD2 += p.ds_temp   * p.ds_temp;
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);  oled.print("Calibrating...");
    oled.setCursor(0, 14); oled.print(i + 1); oled.print(" / "); oled.print(N);
    oled.setCursor(0, 28); oled.print("T:"); oled.print(p.bme_temp, 1);
    oled.setCursor(0, 40); oled.print("H:"); oled.print(p.humidity, 1);
    oled.setCursor(0, 52); oled.print("P:"); oled.print(p.pressure, 1);
    oled.display();
    xSemaphoreGive(i2cMutex);
    delay(1000);
  }
  xSemaphoreTake(baselineMutex, portMAX_DELAY);
  baseline.bme_temp = sumT / N;
  baseline.humidity = sumH / N;
  baseline.pressure = sumP / N;
  baseline.lux      = sumL / N;
  baseline.ds_temp  = sumD / N;
  auto safeStdDev = [](float sumX2, float mean, int n) -> float {
    float v = (sumX2 / n) - (mean * mean);
    float s = sqrtf(v > 0 ? v : 0);
    return s < 0.05f ? 0.05f : s;
  };
  baseline_sigma[0] = safeStdDev(sumT2, baseline.bme_temp, N);
  baseline_sigma[1] = safeStdDev(sumH2, baseline.humidity,  N);
  baseline_sigma[2] = safeStdDev(sumP2, baseline.pressure,  N);
  baseline_sigma[3] = safeStdDev(sumL2, baseline.lux,       N);
  baseline_sigma[4] = safeStdDev(sumD2, baseline.ds_temp,   N);
  xSemaphoreGive(baselineMutex);

  // Seed the latest-sensor queue so taskNetpie has something to read immediately
  SensorPacket_t seed = readSensors();
  xQueueOverwrite(latestSensorQueue, &seed);

  Serial.println("Calibration done.");
  Serial.printf("Baseline T:%.2f H:%.2f P:%.2f L:%.2f D:%.2f\n",
    baseline.bme_temp, baseline.humidity,
    baseline.pressure, baseline.lux, baseline.ds_temp);
}

// ─────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);
  // GPIO modes
  pinMode(PIN_PIR,  INPUT);
  pinMode(PIN_SW1,  INPUT_PULLUP);
  pinMode(PIN_SW2,  INPUT_PULLUP);
  pinMode(PIN_SW3,  INPUT_PULLUP);
  pinMode(PIN_SW4,  INPUT_PULLUP);
  pinMode(PIN_POT,  INPUT);
  // LEDC — new API (ESP32 core v3.x)
  ledcAttach(PIN_LED_RED,    5000, 8);
  ledcAttach(PIN_LED_YELLOW, 5000, 8);
  ledcAttach(PIN_LED_GREEN,  5000, 8);
  // I2C
  Wire.begin(21, 22);
  // OLED
  while (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED failed - check wiring");
    delay(500);
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0); oled.print("OLED OK");
  oled.display();
  delay(200);
  // BME280
  while (!bme.begin(BME_ADDRESS)) {
    oled.setCursor(0, oled.getCursorY());
    oled.print("BME280 failed");
    oled.display();
    delay(500);
  }
  oled.setCursor(0, oled.getCursorY());
  oled.print("BME280 OK");
  oled.display();
  delay(200);
  // TSL2561
  if (!tsl.begin()) {
    oled.setCursor(0, oled.getCursorY());
    oled.print("TSL2561 failed");
    oled.display();
  } else {
    tsl.setGain(TSL2561_GAIN_1X);
    tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);
    oled.setCursor(0, oled.getCursorY());
    oled.print("TSL2561 OK");
    oled.display();
  }
  delay(200);
  // DS18B20
  ds.begin();
  oled.setCursor(0, oled.getCursorY());
  oled.print("DS18B20 OK");
  oled.display();
  delay(200);
  // WiFi
  WiFi.begin(ssid, password);
  oled.setCursor(0, oled.getCursorY());
  oled.print("WiFi connecting...");
  oled.display();
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected.");
  oled.setCursor(0, oled.getCursorY());
  oled.print("WiFi OK");
  oled.display();
  delay(200);

  // NETPIE MQTT — increase buffer for large shadow payload, set keepalive
  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(1024);
  client.setKeepAlive(60);

  // Initial MQTT connect attempt (like the working sketch)
  if (client.connect(mqtt_client, mqtt_username, mqtt_password)) {
    Serial.println("NETPIE connected in setup");
    oled.setCursor(0, oled.getCursorY());
    oled.print("NETPIE OK");
    oled.display();
  } else {
    Serial.printf("NETPIE connect failed rc=%d\n", client.state());
    oled.setCursor(0, oled.getCursorY());
    oled.print("NETPIE FAIL");
    oled.display();
  }
  delay(200);

  // FreeRTOS primitives
  i2cMutex          = xSemaphoreCreateMutex();
  baselineMutex     = xSemaphoreCreateMutex();
  mqttMutex         = xSemaphoreCreateMutex();
  sensorQueue       = xQueueCreate(10, sizeof(SensorPacket_t));
  latestSensorQueue = xQueueCreate(1,  sizeof(SensorPacket_t));
  scoreQueue        = xQueueCreate(1,  sizeof(GhostScore_t));
  ghostEvent        = xEventGroupCreate();

  // Calibrate baseline
  calibrateBaseline();

  // Launch tasks — taskNetpie now at priority 1 (was 0/IDLE)
  xTaskCreatePinnedToCore(taskPIR,         "PIR",     2048, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(taskSensorBurst, "Burst",   4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskScoreEngine, "Score",   4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskDisplay,     "Display", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskNetpie,      "NETPIE",  8192, NULL, 1, NULL, 0);
}

// ─────────────────────────────────────────────────────────────────
// loop() — idle
// ─────────────────────────────────────────────────────────────────
void loop() {
  vTaskDelay(portMAX_DELAY);
}
