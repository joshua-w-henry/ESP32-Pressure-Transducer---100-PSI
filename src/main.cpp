// Water Supply — ESP32-S3 pressure + dual Hall flow meter monitor.
// Pressure: GPIO4/GPIO5, 10k/10k divider + 0.1uF at ADC input.
// Flow pulses: GPIO6/GPIO7, 10k series + 15k to GND (NO signal capacitor).
// All 5V sensors share ESP32 GND. Never apply 5V directly to a GPIO.

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <math.h>

#include "secrets.h"  // private file; copy include/secrets.example.h

namespace Config {
constexpr char HOSTNAME[] = "esp32-well-room";
constexpr char VERSION[] = "0.3.0";
constexpr char CALIBRATION_ID[] = "static_20261110-11_2pt_v1";  // pressure
constexpr char FLOW_CALIBRATION_ID[] = "gredia_G1_nominal_330ppl_v1";
constexpr char TOPIC_STATE[] = "water/well/pressure/state";
constexpr char TOPIC_AVAIL[] = "water/well/pressure/availability";

constexpr uint8_t PIN_UPSTREAM = 4;    // ADC1, before Rusco
constexpr uint8_t PIN_DOWNSTREAM = 5;  // ADC1, after Rusco
constexpr uint8_t PIN_FLOW_1 = 6;     // Flow meter 1 pulse input, 3.3V max
constexpr uint8_t PIN_FLOW_2 = 7;     // Flow meter 2 pulse input, 3.3V max
constexpr float FLOW_1_PULSES_PER_LITER = 330.0f;  // nominal, calibrate later
constexpr float FLOW_2_PULSES_PER_LITER = 330.0f;  // independent coefficient
constexpr double US_GALLONS_PER_LITER = 0.2641720523581484;
constexpr float DIVIDER_MULTIPLIER = 2.0f; // 10k top / 10k bottom
constexpr float EMA_ALPHA = 0.12f;        // ~0.2 s time constant at 40 Hz
constexpr uint32_t SAMPLE_MS = 25;       // 40 pressure samples per second
constexpr uint32_t FLOW_RATE_MS = 1000;   // pulse delta / elapsed time
constexpr uint32_t FLOW_SAVE_RUNNING_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t FLOW_SAVE_IDLE_MS = 3000;
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

// Counts are incremented only in their respective GPIO ISRs. Aligned 32-bit
// loads are atomic on ESP32-S3; unsigned subtraction handles counter wrap.
// Never debounce or analog-smooth these digital pulse inputs.
volatile uint32_t flow1Pulses = 0;
volatile uint32_t flow2Pulses = 0;
void IRAM_ATTR onFlow1Rise() { ++flow1Pulses; }
void IRAM_ATTR onFlow2Rise() { ++flow2Pulses; }

Preferences flowNvs;
bool flowNvsReady = false;
uint64_t flow1BootPulses = 0;
uint64_t flow2BootPulses = 0;
uint32_t lastFlow1Pulses = 0;
uint32_t lastFlow2Pulses = 0;
uint32_t savedFlow1Pulses = 0;
uint32_t savedFlow2Pulses = 0;
uint32_t lastFlowRateMs = 0;
uint32_t lastFlowSaveMs = 0;
uint32_t lastFlowActivityMs = 0;
float flow1Lpm = 0.0f;
float flow2Lpm = 0.0f;

double totalGallons(uint64_t pulses, float pulsesPerLiter) {
  return (static_cast<double>(pulses) / pulsesPerLiter) * Config::US_GALLONS_PER_LITER;
}

// Checkpoint the raw pulse totals, not the derived gallons. Recalibration can
// therefore reprocess all future volume totals without rewriting NVS values.
// Write only after flow becomes idle or every 15 minutes during sustained flow
// to avoid excessive flash wear. Unexpected power loss can lose unsaved pulses.
void saveFlowTotals() {
  if (!flowNvsReady) return;
  const uint32_t snap1 = flow1Pulses;
  const uint32_t snap2 = flow2Pulses;
  bool savedAny = false;
  if (snap1 != savedFlow1Pulses) {
    if (flowNvs.putULong64("m1pulses", flow1BootPulses + snap1) == sizeof(uint64_t)) {
      savedFlow1Pulses = snap1;
      savedAny = true;
    } else {
      Serial.println("Warning: flow meter 1 NVS checkpoint failed");
    }
  }
  if (snap2 != savedFlow2Pulses) {
    if (flowNvs.putULong64("m2pulses", flow2BootPulses + snap2) == sizeof(uint64_t)) {
      savedFlow2Pulses = snap2;
      savedAny = true;
    } else {
      Serial.println("Warning: flow meter 2 NVS checkpoint failed");
    }
  }
  if (savedAny) lastFlowSaveMs = millis();
}

void updateFlowMeters(uint32_t now) {
  const uint32_t elapsed = now - lastFlowRateMs;
  if (elapsed < Config::FLOW_RATE_MS) return;
  const uint32_t snap1 = flow1Pulses;
  const uint32_t snap2 = flow2Pulses;
  const uint32_t count1 = snap1 - lastFlow1Pulses;
  const uint32_t count2 = snap2 - lastFlow2Pulses;
  lastFlow1Pulses = snap1;
  lastFlow2Pulses = snap2;
  lastFlowRateMs = now;
  flow1Lpm = (60000.0f * count1) / (Config::FLOW_1_PULSES_PER_LITER * elapsed);
  flow2Lpm = (60000.0f * count2) / (Config::FLOW_2_PULSES_PER_LITER * elapsed);
  if (count1 || count2) lastFlowActivityMs = now;

  const bool pendingSave = snap1 != savedFlow1Pulses ||
                           snap2 != savedFlow2Pulses;
  const bool dueDuringFlow = now - lastFlowSaveMs >= Config::FLOW_SAVE_RUNNING_MS;
  const bool settledIdle = now - lastFlowActivityMs >= Config::FLOW_SAVE_IDLE_MS;
  if (pendingSave && (dueDuringFlow || settledIdle)) saveFlowTotals();
}

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
                            const char* deviceClass,
                            const char* stateClass = "measurement") {
  String topic = String("homeassistant/sensor/well_pressure_") + slug + "/config";
  String payload;
  payload.reserve(580);
  payload += "{\"name\":\""; payload += name;
  payload += "\",\"unique_id\":\"well_pressure_"; payload += slug;
  payload += "\",\"state_topic\":\""; payload += Config::TOPIC_STATE;
  payload += "\",\"value_template\":\"{{ value_json."; payload += jsonKey;
  payload += " }}\",\"availability_topic\":\""; payload += Config::TOPIC_AVAIL;
  payload += "\",\"unit_of_measurement\":\""; payload += unit;
  payload += "\",\"state_class\":\""; payload += stateClass; payload += "\"";
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
  publishDiscoverySensor("flow1_rate", "Flow Meter 1 Rate", "flow1_gpm", "gal/min", "volume_flow_rate");
  publishDiscoverySensor("flow2_rate", "Flow Meter 2 Rate", "flow2_gpm", "gal/min", "volume_flow_rate");
  publishDiscoverySensor("flow1_total", "Flow Meter 1 Total", "flow1_total_gal", "gal", "water", "total_increasing");
  publishDiscoverySensor("flow2_total", "Flow Meter 2 Total", "flow2_total_gal", "gal", "water", "total_increasing");
  publishDiscoverySensor("flow1_pulses", "Flow Meter 1 Pulses", "flow1_pulses", "pulses", "", "total_increasing");
  publishDiscoverySensor("flow2_pulses", "Flow Meter 2 Pulses", "flow2_pulses", "pulses", "", "total_increasing");
}

void publishMeasurements() {
  if (!mqtt.connected() || !upstream.initialized || !downstream.initialized) return;
  char up[24], down[24], upV[24], downV[24];
  jsonNumber(up, sizeof(up), upstream.pressureFiltered, upstream.valid);
  jsonNumber(down, sizeof(down), downstream.pressureFiltered, downstream.valid);
  jsonNumber(upV, sizeof(upV), upstream.sensorV, true, 3);
  jsonNumber(downV, sizeof(downV), downstream.sensorV, true, 3);

  const uint64_t total1 = flow1BootPulses + flow1Pulses;
  const uint64_t total2 = flow2BootPulses + flow2Pulses;
  char payload[768];
  int size = snprintf(payload, sizeof(payload),
    "{\"upstream_psi\":%s,\"downstream_psi\":%s,"
    "\"upstream_v\":%s,\"downstream_v\":%s,"
    "\"upstream_valid\":%s,\"downstream_valid\":%s,"
    "\"upstream_adc_mv\":%lu,\"downstream_adc_mv\":%lu,"
    "\"flow1_gpm\":%.3f,\"flow2_gpm\":%.3f,"
    "\"flow1_total_gal\":%.4f,\"flow2_total_gal\":%.4f,"
    "\"flow1_pulses\":%llu,\"flow2_pulses\":%llu,"
    "\"flow_calibration_id\":\"%s\","
    "\"calibration_id\":\"%s\","
    "\"rssi_dbm\":%d,\"uptime_s\":%lu}",
    up, down, upV, downV,
    upstream.valid ? "true" : "false", downstream.valid ? "true" : "false",
    static_cast<unsigned long>(upstream.adcMv),
    static_cast<unsigned long>(downstream.adcMv),
    flow1Lpm * Config::US_GALLONS_PER_LITER,
    flow2Lpm * Config::US_GALLONS_PER_LITER,
    totalGallons(total1, Config::FLOW_1_PULSES_PER_LITER),
    totalGallons(total2, Config::FLOW_2_PULSES_PER_LITER),
    static_cast<unsigned long long>(total1),
    static_cast<unsigned long long>(total2),
    Config::FLOW_CALIBRATION_ID, Config::CALIBRATION_ID,
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
    // Preserve flow totals before the OTA-triggered reboot.
    saveFlowTotals();
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
  Serial.printf("FLOW1: %.3f GPM, %lu pulses this boot | "
                "FLOW2: %.3f GPM, %lu pulses this boot\n",
                flow1Lpm * Config::US_GALLONS_PER_LITER,
                static_cast<unsigned long>(flow1Pulses),
                flow2Lpm * Config::US_GALLONS_PER_LITER,
                static_cast<unsigned long>(flow2Pulses));
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

  // Each flow input uses a 10k (yellow to GPIO) / 15k (GPIO to GND)
  // level divider. Unlike the analog pressure inputs, NO 0.1uF capacitor.
  // GPIO6/GPIO7 must never receive the raw 5V pulse signal.
  pinMode(Config::PIN_FLOW_1, INPUT);
  pinMode(Config::PIN_FLOW_2, INPUT);
  flowNvsReady = flowNvs.begin("well-flow", false);
  if (flowNvsReady) {
    flow1BootPulses = flowNvs.getULong64("m1pulses", 0);
    flow2BootPulses = flowNvs.getULong64("m2pulses", 0);
  } else {
    Serial.println("Warning: flow totals will NOT survive reboot (NVS unavailable)");
  }
  attachInterrupt(digitalPinToInterrupt(Config::PIN_FLOW_1), onFlow1Rise, RISING);
  attachInterrupt(digitalPinToInterrupt(Config::PIN_FLOW_2), onFlow2Rise, RISING);

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
  lastFlowRateMs = lastFlowSaveMs = lastFlowActivityMs = millis();
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
  updateFlowMeters(now);

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
