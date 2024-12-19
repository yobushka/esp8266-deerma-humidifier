#include "mqtt_manager.h"
#include <ESP8266WiFi.h>

MqttManager::MqttManager(ConfigManager &c)
: config(c) {}

void MqttManager::begin() {
    mqttClient.onConnect([this](bool sessionPresent) { onMqttConnect(sessionPresent); });
    mqttClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { onMqttDisconnect(reason); });
    mqttClient.setServer(config.mqtt_server, String(config.mqtt_port).toInt());
    if (strlen(config.mqtt_user) > 0) {
      mqttClient.setCredentials(config.mqtt_user, config.mqtt_password);
    }
}

void MqttManager::connectIfNeeded() {
    if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
        mqttClient.connect();
    }
}

void MqttManager::onMqttConnect(bool sessionPresent) {
    // Subscribe to topics if needed
}

void MqttManager::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    // Attempt reconnection as needed
}
