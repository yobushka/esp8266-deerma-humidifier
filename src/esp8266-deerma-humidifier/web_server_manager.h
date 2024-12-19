#pragma once
#include <ESPAsyncWebServer.h>
#include "config_manager.h"
#include "mqtt_manager.h"

class WebServerManager {
public:
    WebServerManager(ConfigManager &config, MqttManager &mqtt);
    void begin(bool settingMode);

private:
    ConfigManager &config;
    MqttManager &mqtt;
    AsyncWebServer server;
    bool settingMode;

    String getStyles();
    String getManifest();
    String getServiceWorker();

    // Handlers
    void handleRoot(AsyncWebServerRequest *request);
    void handleApiStatus(AsyncWebServerRequest *request);
    void handleApiSettings(AsyncWebServerRequest *request);
    void handleApiSetAp(AsyncWebServerRequest *request, uint8_t *data, size_t len);
    void handleApiReset(AsyncWebServerRequest *request);

    // Utility
    String IpAddress2String(const IPAddress& ipAddress);
};
