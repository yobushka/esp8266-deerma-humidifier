#pragma once
#include <FS.h>
#include <ESP8266WiFi.h>

class ConfigManager {
public:
    bool debugEnabled = false;
    bool mqttAuth = true;
    char ssid[40] = "";
    char pass[80] = "";
    char mqtt_server[40] = "";
    char mqtt_port[6] = "1883";
    char mqtt_user[40] = "";
    char mqtt_password[40] = "";
    char HOSTNAME[48] = "ESPDevice";
    char LOCATION[48] = "UNKNWN";

    ConfigManager();
    bool loadConfig();
    bool saveConfig();
    bool isConfigured();
};
