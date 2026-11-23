#pragma once
#include <Arduino.h>

// Wi-Fi Credentials
const char* default_ssid     = "YOUR_WIFI_SSID";
const char* default_password = "YOUR_WIFI_PASSWORD";

// Static IP Configuration
#define DEFAULT_LOCAL_IP IPAddress(192, 168, 1, 6)
#define DEFAULT_GATEWAY  IPAddress(192, 168, 1, 1)
#define DEFAULT_SUBNET   IPAddress(255, 255, 255, 0)
#define DEFAULT_DNS1     IPAddress(192, 168, 1, 1)
#define DEFAULT_DNS2     IPAddress(8, 8, 8, 8)
