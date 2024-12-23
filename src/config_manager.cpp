#include "config_manager.h"
#include <ArduinoJson.h>

ConfigManager::ConfigManager() {}

bool ConfigManager::loadConfig() {
    if (!SPIFFS.begin()) {
        SPIFFS.format();
        SPIFFS.begin();
    }
    if (!SPIFFS.exists("/config.json")) {
        return false;
    }
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) return false;

    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    configFile.close();

    DynamicJsonDocument json(1024);
    auto deserializeError = deserializeJson(json, buf.get());
    if (deserializeError) {
        return false;
    }

    debugEnabled = json["debug"] | false;
    strlcpy(ssid, json["ssid"] | "", sizeof(ssid));
    strlcpy(pass, json["pass"] | "", sizeof(pass));
    strlcpy(mqtt_server, json["mqtt_server"] | "", sizeof(mqtt_server));
    strlcpy(mqtt_port, json["mqtt_port"] | "1883", sizeof(mqtt_port));
    mqttAuth = json["mqtt_auth"] | true;
    strlcpy(mqtt_user, json["mqtt_user"] | "", sizeof(mqtt_user));
    strlcpy(mqtt_password, json["mqtt_password"] | "", sizeof(mqtt_password));
    strlcpy(HOSTNAME, json["HOSTNAME"] | "ESPDevice", sizeof(HOSTNAME));
    strlcpy(LOCATION, json["LOCATION"] | "UNKNWN", sizeof(LOCATION));

    return true;
}

bool ConfigManager::saveConfig() {
    if (!SPIFFS.begin()) {
        SPIFFS.format();
        SPIFFS.begin();
    }
    DynamicJsonDocument json(1024);
    json["debug"] = debugEnabled;
    json["ssid"] = ssid;
    json["pass"] = pass;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_auth"] = mqttAuth;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;
    json["HOSTNAME"] = HOSTNAME;
    json["LOCATION"] = LOCATION;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) return false;
    serializeJson(json, configFile);
    configFile.close();
    return true;
}

bool ConfigManager::isConfigured() {
    return (WiFi.status() == WL_CONNECTED);
}
