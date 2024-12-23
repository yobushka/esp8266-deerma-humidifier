#include "mqtt_manager.h"
#include <ESP8266WiFi.h>
#include <AsyncMqtt_Generic.h>  // The actual definitions

// We'll have a static AsyncMqttClient that the pointer refers to
static AsyncMqttClient mqttClient;

MqttManager::MqttManager(ConfigManager &c)
    : config(c)
{
    // Store a pointer to our static global
    mqttClientPtr = &mqttClient;
}

void MqttManager::begin() {
    mqttClientPtr->onConnect([this](bool sessionPresent) {
        onMqttConnect(sessionPresent);
    });
    mqttClientPtr->onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
        onMqttDisconnect(reason);
    });
    mqttClientPtr->setServer(config.mqtt_server, String(config.mqtt_port).toInt());
    if (strlen(config.mqtt_user) > 0) {
      mqttClientPtr->setCredentials(config.mqtt_user, config.mqtt_password);
    }
    // ...
}

void MqttManager::connectIfNeeded() {
    if (WiFi.status() == WL_CONNECTED && !mqttClientPtr->connected()) {
        mqttClientPtr->connect();
    }
}

void MqttManager::onMqttConnect(bool sessionPresent) {
    // ...
}

void MqttManager::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    // ...
}
