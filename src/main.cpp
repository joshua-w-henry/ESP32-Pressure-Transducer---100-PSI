// Water Supply — ESP32-S3 100 PSI dual-channel pressure monitor
// GPIO4 upstream / GPIO5 downstream. Each signal MUST pass through a
// 10k series resistor; GPIO gets 10k to GND and 0.1uF to GND.
// 5V sensors share ground with the ESP32. Never apply 5V to a GPIO.

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>

#include "secrets.h"  // private file; copy include/secrets.example.h

namespace Config {
constexpr char HOSTNAME[] = "esp32-well-room";
constexpr char VERSION[] = "0.2.0";
constexpr char CALIBRATION_ID[] = "static_20261110-11_2pt_v1";
constexpr char TOPIC_STATE[] = "water/well/pressure/state";
constexpr char TOPIC_AVAIL[] = "water/well/pressure/availability";

constexpr uint8_t PIN_UPSTREAM = 4;    // ADC1, before Rusco
constexpr uint8_t PIN_DOWNSTREAM = 5;  // ADC1, after Rusco
constexpr float DIVIDER_MULTIPLIER = 2.0f; // 10k top / 10k bottom
constexpr float EMA_ALPHA = 0.12f;        // ~0.2 s time constant at 40 Hz
constexpr uint32_t SAMPLE_MS = 25;       // 40 samples per second
constexpr uint32_t SERIAL_MS = 1000;
constexpr uint32_t PUBLISH_MS = 5000;
constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint32_t MQTT_RETRY_MS = 10000;
}

// Provisional two-point static calibration, independently applied per sensor.
// We only have historical displayed *nominal PSI* values, not synchronized
// unrounded voltage logs. Convert prior firmware's nominal model
//    P_nominal = (V_sensor - 0.5V) * 25 PSI/V
// to approximate sensor-output voltage at each matched manual gauge reading.
// Anchor A: gauge 39.50 PSI, upstream 25.20, downstream 21.97 nominal PSI
// Anchor B: gauge 51.50 PSI, upstream 39.59, downstream 36.38 nominal PSI
// Sensor V is still published so the model can be fitted more accurately later.
// These are NOT independently validated accuracy specifications; no-flow only.
struct Calibration {
  float v1;
  float psi1;
  float v2;
  float psi2;
};
constexpr Calibration UPSTREAM_CAL   {1.5080f, 39.50f, 2.0836f, 51.50f};
constexpr Calibration DOWNSTREAM_CAL {1.3788f, 39.50f, 1.9552f, 51.50f};

struct Channel {
  uint8_t pin;
  Calibration cal;
  uint32_t adcMv = 0;          // ESP32 pin, after divider
  float sensorV = 0.0f;        // recovered voltage before divider
  float pressureRaw = 0.0f;
  float pressureFiltered = 0.0f;
  bool initialized = false;
  bool valid = false;

  // PlatformIO's Arduino-ESP32 2.0 toolchain builds with C++11.
  // Default member initializers prevent C++11 aggregate initialization,
  // so provide an explicit constructor for pin + calibration.
  Channel(uint8_t adcPin, const Calibration& calibration)
      : pin(adcPin), cal(calibration) {}
};
Channel upstream   {Config::PIN_UPSTREAM, UPSTREAM_CAL};
Channel downstream {Config::PIN_DOWNSTREAM, DOWNSTREAM_CAL};

WiFiClient network;
PubSubClient mqtt(network);
bool otaStarted = false;
bool otaInitAttempted = false;
bool otaBusy = false;
uint32_t lastSample = 0;
uint32_t lastSerial = 0;
uint32_t lastPublish = 0;
uint32_t lastWifiAttempt = 0;
uint32_t lastMqttAttempt = 0;
String clientId;

float pressureFromVoltage(float voltage, const Calibration& cal) {
  const float spanV = cal.v2 - cal.v1;
  if (fabsf(spanV) < 0.01f) return NAN; // reject bad calibration
  return cal.psi1 + (voltage - cal.v1) * (cal.psi2 - cal.psi1) / spanV;
}

void sampleChannel(Channel& channel) {
  // analogReadMilliVolts uses the ESP32 calibrated ADC path where available.
  // A short average limits sample-to-sample noise; RC network is on the input.
  uint32_t total = 0;
  for (uint8_t i = 0; i < 4; ++i) total += analogReadMilliVolts(channel.pin);
  channel.adcMv = (total + 2) / 4;
  channel.sensorV = (channel.adcMv / 1000.0f) * Config::DIVIDER_MULTIPLIER;
  channel.pressureRaw = pressureFromVoltage(channel.sensorV, channel.cal);

  // Detect gross short/open/overrange conditions. A floating input can still
  // appear valid: this is diagnostic telemetry, not a safety mechanism.
  channel.valid = isfinite(channel.pressureRaw)
      && channel.sensorV >= 0.20f && channel.sensorV <= 4.80f
      && channel.pressureRaw >= -10.0f && channel.pressureRaw <= 110.0f;

  if (channel.valid) {
    if (!channel.initialized) channel.pressureFiltered = channel.pressureRaw;
    else channel.pressureFiltered += Config::EMA_ALPHA *
                                     (channel.pressureRaw - channel.pressureFiltered);
  }
  channel.initialized = true;
}

// Use JSON null for invalid measurements, rather than misreporting 0 PSI.
void jsonNumber(char* dest, size_t len, float value, bool valid, int decimals = 2) {
  if (!valid || !isfinite(value)) {
    snprintf(dest, len, "null");
    return;
  }
  snprintf(dest, len, "%.*f", decimals, value);
}

void publishDiscoverySensor(const char* slug, const char* name,
                            const char* jsonKey, const char* unit,
                            const char* deviceClass) {
  String topic = String("homeassistant/sensor/well_pressure_") + slug + "/config";
  String payload;
  payload.reserve(580);
  payload += "{\"name\":\""; payload += name;
  payload += "\",\"unique_id\":\"well_pressure_"; payload += slug;
  payload += "\",\"state_topic\":\""; payload += Config::TOPIC_STATE;
  payload += "\",\"value_template\":\"{{ value_json."; payload += jsonKey;
  payload += " }}\",\"availability_topic\":\""; payload += Config::TOPIC_AVAIL;
  payload += "\",\"unit_of_measurement\":\""; payload += unit;
  payload += "\",\"state_class\":\"measurement\"";
  if (strlen(deviceClass)) {
    payload += ",\"device_class\":\""; payload += deviceClass; payload += "\"";
  }
  payload += ",\"device\":{\"identifiers\":[\"well-pressure\"],";
  payload += "\"name\":\"ESP32 Well Room\",";
  payload += "\"manufacturer\":\"DIY / Water Supply\",";
  payload += "\"model\":\"ESP32-S3 dual 100 PSI\",";
  payload += "\"sw_version\":\""; payload += Config::VERSION; payload += "\"}}";
  if (!mqtt.publish(topic.c_str(), payload.c_str(), true)) {
    Serial.printf("MQTT discovery failed: %s\n", slug);
  }
}

void publishDiscovery() {
  // Remove the obsolete retained Home Assistant discovery config from v0.1.
  // This retires the misleading Rusco differential entity, without touching
  // historical recorder data. MQTT discovery deletion uses a zero-length payload.
  if (!mqtt.publish("homeassistant/sensor/well_pressure_filter_dp/config", "", true)) {
    Serial.println("Failed to clear legacy differential discovery config");
  }
  publishDiscoverySensor("upstream",   "Upstream Pressure",    "upstream_psi",    "psi", "pressure");
  publishDiscoverySensor("downstream", "Downstream Pressure",  "downstream_psi",  "psi", "pressure");
  publishDiscoverySensor("up_volts",   "Upstream Sensor Volts", "upstream_v",      "V", "voltage");
  publishDiscoverySensor("down_volts", "Downstream Sensor Volts", "downstream_v", "V", "voltage");
  publishDiscoverySensor("wifi_rssi",  "Wi-Fi RSSI",           "rssi_dbm",       "dBm", "signal_strength");
}

void publishMeasurements() {
  if (!mqtt.connected() || !upstream.initialized || !downstream.initialized) return;
  char up[24], down[24], upV[24], downV[24];
  jsonNumber(up, sizeof(up), upstream.pressureFiltered, upstream.valid);
  jsonNumber(down, sizeof(down), downstream.pressureFiltered, downstream.valid);
  jsonNumber(upV, sizeof(upV), upstream.sensorV, true, 3);
  jsonNumber(downV, sizeof(downV), downstream.sensorV, true, 3);

  char payload[400];
  int size = snprintf(payload, sizeof(payload),
    "{\"upstream_psi\":%s,\"downstream_psi\":%s,"
    "\"upstream_v\":%s,\"downstream_v\":%s,"
    "\"upstream_valid\":%s,\"downstream_valid\":%s,"
    "\"upstream_adc_mv\":%lu,\"downstream_adc_mv\":%lu,"
    "\"calibration_id\":\"%s\","
    "\"rssi_dbm\":%d,\"uptime_s\":%lu}",
    up, down, upV, downV,
    upstream.valid ? "true" : "false", downstream.valid ? "true" : "false",
    static_cast<unsigned long>(upstream.adcMv),
    static_cast<unsigned long>(downstream.adcMv),
    Config::CALIBRATION_ID,
    WiFi.RSSI(), static_cast<unsigned long>(millis() / 1000UL));
  if (size <= 0 || size >= static_cast<int>(sizeof(payload))) {
    Serial.println("MQTT payload overflow");
    return;
  }
  if (!mqtt.publish(Config::TOPIC_STATE, payload, true)) {
    Serial.println("MQTT measurement publish failed");
  }
}

void beginOTA() {
  otaInitAttempted = true;
  ArduinoOTA.setHostname(Config::HOSTNAME);
  ArduinoOTA.setPort(3232);
  // Never start an unauthenticated OTA service.
  if (strlen(OTA_PASSWORD) < 8 || strcmp(OTA_PASSWORD, "CHANGE_ME") == 0) {
    Serial.println("OTA DISABLED: set a strong OTA_PASSWORD in secrets.h");
    return;
  }
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    otaBusy = true;
    if (mqtt.connected()) mqtt.disconnect();
    Serial.println("OTA update started");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA update complete"); });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("OTA error %u\n", static_cast<unsigned>(err));
    otaBusy = false;
  });
  ArduinoOTA.begin();
  otaStarted = true;
  Serial.printf("ArduinoOTA ready: %s.local:3232\n", Config::HOSTNAME);
}

void connectWifiIfNeeded(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaInitAttempted) {
      Serial.printf("Wi-Fi connected. IP=%s RSSI=%d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      beginOTA();
    }
    return;
  }
  if (now - lastWifiAttempt < Config::WIFI_RETRY_MS) return;
  lastWifiAttempt = now;
  Serial.printf("Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqttIfNeeded(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (now - lastMqttAttempt < Config::MQTT_RETRY_MS) return;
  lastMqttAttempt = now;

  bool ok = false;
  if (strlen(MQTT_USERNAME)) {
    ok = mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                      Config::TOPIC_AVAIL, 0, true, "offline");
  } else {
    ok = mqtt.connect(clientId.c_str(), Config::TOPIC_AVAIL, 0, true, "offline");
  }

  if (ok) {
    Serial.println("MQTT connected");
    mqtt.publish(Config::TOPIC_AVAIL, "online", true);
    publishDiscovery();
    publishMeasurements();
  } else {
    Serial.printf("MQTT unavailable (state %d); retrying\n", mqtt.state());
  }
}

void printDiagnostics() {
  Serial.printf(
    "UP: ADC=%lu mV out=%.3f V psi=%.2f %s | "
    "DN: ADC=%lu mV out=%.3f V psi=%.2f %s | WiFi=%s MQTT=%s\n",
    static_cast<unsigned long>(upstream.adcMv), upstream.sensorV,
    upstream.pressureFiltered, upstream.valid ? "OK" : "FAULT",
    static_cast<unsigned long>(downstream.adcMv), downstream.sensorV,
    downstream.pressureFiltered, downstream.valid ? "OK" : "FAULT",
    WiFi.status() == WL_CONNECTED ? "up" : "down",
    mqtt.connected() ? "up" : "down");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nWater Supply pressure monitor v%s | calibration: %s\n",
                Config::VERSION, Config::CALIBRATION_ID);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::PIN_UPSTREAM, ADC_11db);
  analogSetPinAttenuation(Config::PIN_DOWNSTREAM, ADC_11db);
  pinMode(Config::PIN_UPSTREAM, INPUT);
  pinMode(Config::PIN_DOWNSTREAM, INPUT);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);   // required for HA discovery configurations
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(2);

  clientId = String("well-pressure-") +
             String(static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL), HEX);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(Config::HOSTNAME);
  WiFi.setAutoReconnect(true);
  // First attempt now, retries every 10s without rebooting/offline data loss.
  lastWifiAttempt = millis() - Config::WIFI_RETRY_MS;
  lastMqttAttempt = millis() - Config::MQTT_RETRY_MS;
  lastSample = lastSerial = lastPublish = millis();
}

void loop() {
  const uint32_t now = millis();
  connectWifiIfNeeded(now);
  if (otaStarted) ArduinoOTA.handle();
  if (otaBusy) return;  // no network traffic/sampling during flash update

  if (now - lastSample >= Config::SAMPLE_MS) {
    lastSample = now;
    sampleChannel(upstream);
    sampleChannel(downstream);
  }

  connectMqttIfNeeded(now);
  if (mqtt.connected()) mqtt.loop();

  if (now - lastSerial >= Config::SERIAL_MS) {
    lastSerial = now;
    if (upstream.initialized && downstream.initialized) printDiagnostics();
  }
  if (now - lastPublish >= Config::PUBLISH_MS) {
    lastPublish = now;
    publishMeasurements();
  }
}
