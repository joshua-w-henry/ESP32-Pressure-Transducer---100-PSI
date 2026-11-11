#pragma once

// Copy to include/secrets.h and edit. NEVER commit secrets.h.
// Wi-Fi uses DHCP: reserve the assigned IP on Keeper of the Hounds.
#define WIFI_SSID "Keeper of the Hounds"
#define WIFI_PASSWORD "CHANGE_ME"

// Mosquitto broker on RTR: use its reachable LAN IP or DNS name.
// Example: "192.168.86.10" (replace with ACTUAL address).
#define MQTT_HOST "192.168.86.X"
#define MQTT_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""

// Strong unique password (8+ chars). Used by ArduinoOTA and local OTA uploader.
#define OTA_PASSWORD "CHANGE_ME"
