#pragma once
#include "config_manager.h"

// Forward declare the enum properly:
enum class AsyncMqttClientDisconnectReason : uint8_t;

// Forward declare AsyncMqttClient so we don't pull in the entire library here
class AsyncMqttClient;

class MqttManager {
public:
    MqttManager(ConfigManager &config);
    void begin();
    void connectIfNeeded();

private:
    ConfigManager &config;
    AsyncMqttClient* mqttClientPtr;  // Pointer

    void onMqttConnect(bool sessionPresent);
    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
};
