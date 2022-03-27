#include "types.h"
#include "wifi.h"

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <SoftwareSerial.h>

/*
Firmware documentation: https://iot.mi.com/new/doc/embedded-development/wifi/module-dev/serial-communication
*/

humidifierState_t state;

char serialRxBuf[255];
char serialTxBuf[255];
char debugRxBuf[255];

SoftwareSerial SerialDebug(3, 1);

WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient mqttClient;

WiFiManagerParameter custom_mqtt_server("server", "MQTT server", mqtt_server, sizeof(mqtt_server));
WiFiManagerParameter custom_mqtt_user("user", "MQTT username", username, sizeof(username));
WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", password, sizeof(password));

unsigned long lastMqttConnectionAttempt = millis();
const long mqttConnectionInterval = 60000;

unsigned long stateUpdatePreviousMillis = millis();
const long stateUpdateInterval = 60 * 1000; // 60 seconds

char identifier[24];
#define FIRMWARE_PREFIX "esp8266-deerma-humidifier"
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"

char MQTT_TOPIC_AVAILABILITY[128];
char MQTT_TOPIC_STATE[128];
char MQTT_TOPIC_COMMAND[128];
char MQTT_TOPIC_COMMAND_RAW[128];

#define PROP_POWER_SIID 2
#define PROP_POWER_PIID 1

#define PROP_READ_SIID 3
#define PROP_HUMIDITY_PIID 1
#define PROP_TEMPERATURE_PIID 7

#define PROP_SET_SIID 2
#define PROP_HUMIDITY_MODE_PIID 5
#define PROP_HUMIDITY_SETPOINT_PIID 6

#define PROP_SOUND_SIID 5
#define PROP_SOUND_ENABLED_PIID 1

#define PROP_LED_SIID 6
#define PROP_LED_ENABLED_PIID 1

#define PROP_WATER_TANK_SIID 7
#define PROP_WATER_TANK_EMPTY_PIID 1
#define PROP_WATER_TANK_REMOVED_PIID 2

const int DOWNSTREAM_QUEUE_SIZE = 50;
const int DOWNSTREAM_QUEUE_ELEM_SIZE = 51;

char downstreamQueue[DOWNSTREAM_QUEUE_SIZE][DOWNSTREAM_QUEUE_ELEM_SIZE];
int downstreamQueueIndex = -1;
char nextDownstreamMessage[DOWNSTREAM_QUEUE_ELEM_SIZE];

boolean shouldUpdateState = false;

void clearDownstreamQueueAtIndex(int index) {
  memset(downstreamQueue[index], 0, sizeof(downstreamQueue[index]));
}

void queueDownstreamMessage(char *message) {
  if (downstreamQueueIndex >= DOWNSTREAM_QUEUE_SIZE - 1) {
    SerialDebug.printf("Error: Queue is full. Dropping message: <%s>\n", message);
    return;
  } 
  
  downstreamQueueIndex++;
  snprintf(downstreamQueue[downstreamQueueIndex], sizeof(downstreamQueue[downstreamQueueIndex]), "%s", message);
}

void fillNextDownstreamMessage() {
  memset(nextDownstreamMessage, 0, sizeof(nextDownstreamMessage));

  if (downstreamQueueIndex < 0) {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, "none");

  } else if (downstreamQueueIndex == 0) {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[0]);
    clearDownstreamQueueAtIndex(0);
    downstreamQueueIndex--;

  } else {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[0]);
    for (int i = 0; i < downstreamQueueIndex; i++) {
      snprintf(downstreamQueue[i], DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[i + 1]);
    }

    clearDownstreamQueueAtIndex(downstreamQueueIndex);
    downstreamQueueIndex--;
  }
}


void handleUart() {
  if (!Serial.available()) {
    return;
  }

  memset(serialRxBuf, 0, sizeof(serialRxBuf));
  Serial.readBytesUntil('\r', serialRxBuf, 250);

  // SerialDebug.printf("Received from UART: <%s>\n", serialRxBuf);

  if (strncmp(serialRxBuf, "properties_changed", 18) == 0) {
    int propSiid = 0;
    int propPiid = 0;
    char propValue[10];

    int propChanged = sscanf(serialRxBuf, "properties_changed %d %d %s", &propSiid, &propPiid, propValue);
    if (propChanged == 3) {
      if (propSiid == PROP_POWER_SIID && propPiid == PROP_POWER_PIID) {
        state.powerOn = strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New power status: <%s>\n", state.powerOn ? "on" : "off");

      } else if (propSiid == PROP_SET_SIID && propPiid == PROP_HUMIDITY_MODE_PIID) {
        state.mode = (humMode_t)atoi(propValue);
        SerialDebug.printf("New humidityMode: <%d>\n", state.mode);

      } else if (propSiid == PROP_SET_SIID && propPiid == PROP_HUMIDITY_SETPOINT_PIID) {
        state.humiditySetpoint = atoi(propValue);
        SerialDebug.printf("New humiditySetpoint: <%d>\n", state.humiditySetpoint);

      } else if (propSiid == PROP_READ_SIID && propPiid == PROP_HUMIDITY_PIID) {
        state.currentHumidity = atoi(propValue);
        SerialDebug.printf("New currentHumidity: <%d>\n", state.currentHumidity);
      
      } else if (propSiid = PROP_READ_SIID && propPiid == PROP_TEMPERATURE_PIID) {
        state.currentTemperature = atoi(propValue);
        SerialDebug.printf("New currentTemperature: <%d>\n", state.currentTemperature);
      
      } else if (propSiid = PROP_LED_SIID && propPiid == PROP_LED_ENABLED_PIID) {
        state.ledEnabled = strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New ledEnabled: <%s>\n", state.ledEnabled ? "true" : "false");
      
      } else if (propSiid = PROP_SOUND_SIID && propPiid == PROP_SOUND_ENABLED_PIID) {
        state.soundEnabled = strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New soundEnabled: <%s>\n", state.soundEnabled ? "true" : "false");
      
      } else if (propSiid = PROP_WATER_TANK_SIID && propPiid == PROP_WATER_TANK_REMOVED_PIID) {
        state.waterTankInstalled = !(strncmp(propValue, "true", 4) == 0);
        SerialDebug.printf("New waterTankInstalled: <%s>\n", state.waterTankInstalled ? "true" : "false");
      
      } else if (propSiid = PROP_WATER_TANK_SIID && propPiid == PROP_WATER_TANK_EMPTY_PIID) {
        state.waterTankEmpty =  strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New waterTankEmpty: <%s>\n", state.waterTankEmpty ? "true" : "false");
      } else  {
        SerialDebug.printf("Unknown property: <%s>\n", serialRxBuf);
      }

      shouldUpdateState = true;
    }

    Serial.print("ok\r");
    return;
  }

  if (shouldUpdateState == true) { 
    shouldUpdateState = false;
    publishState();
  }
  
  if (strncmp(serialRxBuf, "get_down", 8) == 0) {
    fillNextDownstreamMessage();

     if (strncmp(nextDownstreamMessage, "none", 4) != 0) {
      SerialDebug.printf("Sending: %s\n", nextDownstreamMessage);
    }
    
    memset(serialTxBuf, 0, sizeof(serialTxBuf));
    snprintf(serialTxBuf, sizeof(serialTxBuf), "down %s\r", nextDownstreamMessage);
    Serial.print(serialTxBuf);

    return;
  }
  
  if (strncmp(serialRxBuf, "net", 3) == 0) {
    Serial.print("cloud\r");
    return;
  }
  
  if (
    strncmp(serialRxBuf, "mcu_version", 11) == 0 ||
    strncmp(serialRxBuf, "model", 5) == 0 ||
    strncmp(serialRxBuf, "event_occured", 13) == 0 ||
    strncmp(serialRxBuf, "result", 6) == 0
  ) {
    Serial.print("ok\r");
    return;
  }
  
  SerialDebug.printf("Error: Unknown command received: %s\n", serialRxBuf);
}

void queuePropertyChange(int siid, int piid, const char *value) {
  char msg[DOWNSTREAM_QUEUE_ELEM_SIZE];
  snprintf(msg, sizeof(msg), "set_properties %d %d %s", siid, piid, value);
  queueDownstreamMessage(msg);
}

void queuePropertyChange(int siid, int piid, char *value) {
  char msg[DOWNSTREAM_QUEUE_ELEM_SIZE];
  snprintf(msg, sizeof(msg), "set_properties %d %d %s", siid, piid, value);
  queueDownstreamMessage(msg);
}

void setPowerState(boolean powerOn) {
  queuePropertyChange(PROP_POWER_SIID, PROP_POWER_PIID, powerOn ? "true" : "false");
}

void setLEDState(boolean ledEnabled) {
  queuePropertyChange(PROP_LED_SIID, PROP_LED_ENABLED_PIID, ledEnabled ? "true" : "false");
}

void setSoundState(boolean soundEnabled) {
  queuePropertyChange(PROP_SOUND_SIID, PROP_SOUND_ENABLED_PIID, soundEnabled ? "true" : "false");
}

void setHumidityMode(humMode_t mode) {
  char modeStr[1];
  sprintf(modeStr, "%d", mode); 
  queuePropertyChange(PROP_SET_SIID, PROP_HUMIDITY_MODE_PIID, modeStr);
}

void setHumiditySetpoint(uint8_t value) {
  char valueStr[1];
  sprintf(valueStr, "%d", value); 
  queuePropertyChange(PROP_SET_SIID, PROP_HUMIDITY_SETPOINT_PIID, valueStr);
//  state.humiditySetpoint = (int)value;
}

void sendNetworkStatus(boolean isConnected) {
  queueDownstreamMessage("MIIO_net_change cloud");
}

bool shouldSaveConfig = false;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  Serial.swap();
  delay(1000);

  SerialDebug.begin(9600); 

  snprintf(identifier, sizeof(identifier), "HUMIDIFIER-%X", ESP.getChipId());
  SerialDebug.printf("Identifier: %s\n", identifier);

  snprintf(MQTT_TOPIC_AVAILABILITY, 127, "%s/%s/status", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_STATE, 127, "%s/%s/state", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_COMMAND, 127, "%s/%s/command", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_COMMAND_RAW, 127, "%s/%s/rawcommand", FIRMWARE_PREFIX, identifier);

  sendNetworkStatus(false);

  WiFi.hostname(identifier);

  loadConfig();

  setupWifi();
  SerialDebug.println("Setup WiFi ok");

  setupOTA();
  SerialDebug.println("Setup OTA ok");

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setKeepAlive(10);
  mqttClient.setBufferSize(2048);
  mqttClient.setCallback(mqttCallback);
  mqttReconnect();

  SerialDebug.println("Setup MQTT ok");
}

void setupOTA() {
  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.setHostname(identifier);

  //This is less of a security measure and more a accidential flash prevention
  ArduinoOTA.setPassword(identifier);
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  
  handleUart();
  mqttClient.loop();

  if (!mqttClient.connected() && (mqttConnectionInterval <= (millis() - lastMqttConnectionAttempt)) )  {
    // Serial.println("Connection to MQTT lost, reconnecting...Debug");
    lastMqttConnectionAttempt = millis();
    mqttReconnect();
  }

  if (stateUpdateInterval <= (millis() - stateUpdatePreviousMillis)) {
    publishState();
  }
}

void setupWifi() {
  wifiManager.setDebugOutput(false);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);

  WiFi.hostname(identifier);
  wifiManager.autoConnect(identifier);
  mqttClient.setClient(wifiClient);

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(username, custom_mqtt_user.getValue());
  strcpy(password, custom_mqtt_pass.getValue());

  if (shouldSaveConfig) {
    saveConfig();
  } else {
    //For some reason, the read values get overwritten in this function
    //To combat this, we just reload the config
    //This is most likely a logic error which could be fixed otherwise
    loadConfig();
  }
}

void resetWifiSettingsAndReboot() {
  wifiManager.resetSettings();
  sendNetworkStatus(false);
  delay(3000);
  ESP.restart();
}

void mqttReconnect() {
  SerialDebug.printf("Connecting to MQTT broker on <%s> (%s : %s)\n", mqtt_server, username, password);

  if (mqttClient.connect(identifier, username, password, MQTT_TOPIC_AVAILABILITY, 1, true, AVAILABILITY_OFFLINE)) {
    sendNetworkStatus(true);

    mqttClient.publish(MQTT_TOPIC_AVAILABILITY, AVAILABILITY_ONLINE, true);

    mqttClient.subscribe(MQTT_TOPIC_COMMAND);
    mqttClient.subscribe(MQTT_TOPIC_COMMAND_RAW);

    SerialDebug.println("Connection succeded to MQTT broker");
 } else {
    SerialDebug.println("Unable to connect to MQTT broker");
  }
}

boolean isMqttConnected() {
  return mqttClient.connected();
}

void publishState() {
  SerialDebug.println("Publishing new state");

  DynamicJsonDocument wifiJson(192);
  DynamicJsonDocument stateJson(604);
  char payload[256];

  wifiJson["ssid"] = WiFi.SSID();
  wifiJson["ip"] = WiFi.localIP().toString();
  wifiJson["rssi"] = WiFi.RSSI();

  stateJson["state"] = state.powerOn ? "on" : "off";

  switch (state.mode) {
    case (humMode_t)setpoint:
      stateJson["mode"] = "setpoint";
      break;
    case (humMode_t)low:
      stateJson["mode"] = "low";
      break;
    case (humMode_t)medium:
      stateJson["mode"] = "medium";
      break;
    case (humMode_t)high:
      stateJson["mode"] = "high";
      break;
    default:
      stateJson["mode"] = "unknown";
  }

  stateJson["humiditySetpoint"] = state.humiditySetpoint;

  stateJson["humidity"] = state.currentHumidity;
  stateJson["temperature"] = state.currentTemperature;

  stateJson["sound"] = state.soundEnabled ? "on" : "off";
  stateJson["led"] = state.ledEnabled ? "on" : "off";

  stateJson["waterTank"] = state.waterTankInstalled ? (state.waterTankEmpty ? "empty" : "full") : "missing";

  stateJson["wifi"] = wifiJson.as<JsonObject>();

  serializeJson(stateJson, payload);
  mqttClient.publish(MQTT_TOPIC_STATE, payload, true);

  stateUpdatePreviousMillis = millis();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_COMMAND_RAW) == 0) {
    char payloadText[length + 1];
    snprintf(payloadText, length + 1, "%s", payload);

    SerialDebug.printf("Received MQTT RAW command: <%s>\n", payloadText);

    queueDownstreamMessage(payloadText);

    return;
  }
  
  if (strcmp(topic, MQTT_TOPIC_COMMAND) == 0) {
    DynamicJsonDocument commandJson(256);
    char payloadText[length + 1];

    snprintf(payloadText, length + 1, "%s", payload);

    DeserializationError err = deserializeJson(commandJson, payloadText);

    SerialDebug.printf("Received MQTT command: <%s>\n", topic, payloadText);

    if (err) {
      SerialDebug.println("Error deserializing JSON");
      return;
    }

    String stateCommand = commandJson["state"].as<String>();
    String modeCommand = commandJson["mode"].as<String>();
    String soundCommand = commandJson["sound"].as<String>();
    String ledCommand = commandJson["led"].as<String>();
    String command = commandJson["command"].as<String>();

    long humiditySetpointCommand = commandJson["humiditySetpoint"] | -1;

    if (stateCommand == "off") {
      setPowerState(false);
    } else if (stateCommand == "on") {
      setPowerState(true);
    }

    if (modeCommand == "low") {
      setHumidityMode((humMode_t)low);
    } else if (modeCommand == "medium") {
      setHumidityMode((humMode_t)medium);
    } else if (modeCommand == "high") {
      setHumidityMode((humMode_t)high);
    } else if (modeCommand == "setpoint") {
      setHumidityMode((humMode_t)setpoint);
    }

    if (soundCommand == "off") {
      setSoundState(false);
    } else if (soundCommand == "on") {
      setSoundState(true);
    }

    if (ledCommand == "off") {
      setLEDState(false);
    } else if (ledCommand == "on") {
      setLEDState(true);
    }

    if (humiditySetpointCommand > -1) {
      setHumiditySetpoint((uint8_t)humiditySetpointCommand);
    }

    if (command == "reboot") {
      ESP.restart();
    }
    
  }
}

void saveConfig() {
  DynamicJsonDocument json(512);
  json["mqtt_server"] = mqtt_server;
  json["username"] = username;
  json["password"] = password;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    SerialDebug.println("Failed to open config file for writing");
    return;
  }

  serializeJson(json, configFile);
  configFile.close();
}

void loadConfig() {
  SerialDebug.println("Loading config");

  if (!SPIFFS.begin()) {
    SerialDebug.println("Failed to open SPIFFS");
    return;
  }

  if (SPIFFS.exists("/config.json")) {
    SerialDebug.println("Config file not found");
    return;
  }

  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    SerialDebug.println("Failed to open config file");
    return;
  }

  const size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  DynamicJsonDocument json(512);

  if (DeserializationError::Ok != deserializeJson(json, buf.get())) {
    SerialDebug.println("Failed to parse config fileDebug");
    return;
  }

  strcpy(mqtt_server, json["mqtt_server"]);
  strcpy(username, json["username"]);
  strcpy(password, json["password"]);

  SerialDebug.printf("MQTT config loaded: <%s>, username: <%s>, password: <%s>\n", mqtt_server, username, password);  
}
