#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "uart_manager.h"
#include "device_manager.h"
#include "config_manager.h"
#include "mqtt_manager.h"
#include "web_server_manager.h"

ConfigManager config;
MqttManager mqtt(config);
WebServerManager web(config, mqtt);
AsyncUartManager asyncUart(Serial);  // using hardware Serial
DeviceManager devMgr(asyncUart);

bool settingMode = false;

void onWiFiConnect(const WiFiEventStationModeGotIP& event) {
    mqtt.connectIfNeeded();
}

void setup() {

    Serial.begin(115200);
    delay(1000);

    // Start the “async” approach
    asyncUart.begin();

    // Let device manager do any init
    devMgr.begin();
/*
    // Send some initial commands
    devMgr.setPower(true);
    devMgr.setMode(HumMode::Low);
    devMgr.setPower(false); // etc...
*/

    config.loadConfig();

    WiFi.disconnect();
    if (strlen(config.ssid) > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(config.ssid, config.pass);
    } else {
        // No config, AP mode
        settingMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(config.HOSTNAME);
    }

    // Register WiFi event to know when we got IP and connect to MQTT then
    wifi_station_set_auto_connect(true);
    static WiFiEventHandler gotIpEventHandler = WiFi.onStationModeGotIP(&onWiFiConnect);

    mqtt.begin();
    web.begin(settingMode);
}

void loop() {
    asyncUart.poll(); // async... :D
    // With Async libraries, no code needed in loop
}
