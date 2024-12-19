#pragma once
#include <AsyncMqttClient.h>
#include "config_manager.h"

class MqttManager {
public:
    MqttManager(ConfigManager &config);
    void begin();
    void connectIfNeeded();

private:
    ConfigManager &config;
    AsyncMqttClient mqttClient;

    void onMqttConnect(bool sessionPresent);
    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
};
