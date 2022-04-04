#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>

/*
Firmware documentation: https://iot.mi.com/new/doc/embedded-development/wifi/module-dev/serial-communication
*/

#define FLASH_PIN 0
#define STATUS_PIN 16

#define MQTT_CONN_RETRY_INTERVAL 60000
#define MQTT_STATUS_TIMEOUT 60000

SoftwareSerial SerialDebug(3, 1);

unsigned long currentTime = millis();

const char *BOARD_PREFIX = "esp8266-deerma-humidifier";
char BOARD_IDENTIFIER[64];
char mqtt_server[64];
char mqtt_username[64];
char mqtt_password[64];

#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"

char MQTT_TOPIC_STATUS[128];
char MQTT_TOPIC_STATE[128];
char MQTT_TOPIC_COMMAND[128];
char MQTT_TOPIC_DEBUG[128];

WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient mqttClient;

WiFiManagerParameter wifi_param_mqtt_server("server", "MQTT server", mqtt_server, sizeof(mqtt_server));
WiFiManagerParameter wifi_param_mqtt_username("user", "MQTT username", mqtt_username, sizeof(mqtt_username));
WiFiManagerParameter wifi_param_mqtt_password("pass", "MQTT password", mqtt_password, sizeof(mqtt_password));

unsigned long lastMqttConnectionAttempt = millis();
unsigned long stateUpdatePreviousMillis = millis();

enum humMode_t
{
  unknown = -1,
  low = 1,
  medium = 2,
  high = 3,
  setpoint = 4
};
struct humidifierState_t
{
  boolean powerOn;

  humMode_t mode = (humMode_t)-1;

  int humiditySetpoint = -1;

  int currentHumidity = -1;
  int currentTemperature = -1;

  boolean soundEnabled;
  boolean ledEnabled;

  boolean waterTankInstalled;
  boolean waterTankEmpty;
};

humidifierState_t state;

char serialRxBuf[255];
char serialTxBuf[255];

boolean DebugEnabled = false;
boolean UARTEnabled = true;

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

void saveConfig()
{
  SerialDebug.println("Saving config...");

  DynamicJsonDocument json(512);
  json["mqtt_server"] = wifi_param_mqtt_server.getValue();
  json["mqtt_username"] = wifi_param_mqtt_username.getValue();
  json["mqtt_password"] = wifi_param_mqtt_password.getValue();
  ;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    SerialDebug.println("Failed to open config file for writing");
    return;
  }

  SerialDebug.printf("Saving JSON: %s\n", json.as<String>().c_str());

  serializeJson(json, configFile);
  configFile.close();

  SerialDebug.println("Config saved, please reboot");
}

void loadConfig()
{
  SerialDebug.println("Loading config");

  if (!SPIFFS.begin())
  {
    SerialDebug.println("Failed to open SPIFFS");
    return;
  }

  if (!SPIFFS.exists("/config.json"))
  {
    SerialDebug.println("Config file not found, please configure the ESP by connecting to its Wi-Fi hotspot");
    return;
  }

  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile)
  {
    SerialDebug.println("Failed to open config file");
    return;
  }

  const size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  DynamicJsonDocument json(512);

  if (DeserializationError::Ok != deserializeJson(json, buf.get()))
  {
    SerialDebug.println("Failed to parse config fileDebug");
    return;
  }

  strcpy(mqtt_server, json["mqtt_server"]);
  strcpy(mqtt_username, json["mqtt_username"]);
  strcpy(mqtt_password, json["mqtt_password"]);

  wifi_param_mqtt_server.setValue(mqtt_server, sizeof(mqtt_server));
  wifi_param_mqtt_username.setValue(mqtt_username, sizeof(mqtt_username));
  wifi_param_mqtt_password.setValue(mqtt_password, sizeof(mqtt_password));

  SerialDebug.printf("Config JSON: %s\n", json.as<String>().c_str());
}

void setupMDNS()
{
  if (MDNS.begin(BOARD_IDENTIFIER))
  {
    SerialDebug.println("MDNS responder started");
  }
  else
  {
    SerialDebug.println("MDNS responder got an error");
  }
}

void setupGeneric()
{
  SerialDebug.begin(9600);
  SerialDebug.println("BOOT");

  pinMode(STATUS_PIN, OUTPUT);
  digitalWrite(STATUS_PIN, HIGH);

  pinMode(FLASH_PIN, INPUT_PULLUP);

  snprintf(BOARD_IDENTIFIER, sizeof(BOARD_IDENTIFIER), "%s-%X", BOARD_PREFIX, ESP.getChipId());
  SerialDebug.printf("Board Identifier: %s\n", BOARD_IDENTIFIER);

  snprintf(MQTT_TOPIC_STATUS, 127, "%s/status", BOARD_IDENTIFIER);
  snprintf(MQTT_TOPIC_STATE, 127, "%s/state", BOARD_IDENTIFIER);
  snprintf(MQTT_TOPIC_COMMAND, 127, "%s/command", BOARD_IDENTIFIER);
  snprintf(MQTT_TOPIC_DEBUG, 127, "%s/debug", BOARD_IDENTIFIER);

  loadConfig();
}

bool portalRunning = false;

void setupWifi()
{
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setDebugOutput(false);
  wifiManager.setSaveParamsCallback(saveConfig);

  wifiManager.addParameter(&wifi_param_mqtt_server);
  wifiManager.addParameter(&wifi_param_mqtt_username);
  wifiManager.addParameter(&wifi_param_mqtt_password);

  if (wifiManager.autoConnect(BOARD_IDENTIFIER))
  {
    WiFi.mode(WIFI_STA);
    wifiManager.startWebPortal();
  }
  else
  {
    SerialDebug.println("Failed to connect to WiFi, starting AP");
  }
}

void loopWifi()
{
  wifiManager.process();

  if (digitalRead(FLASH_PIN) == LOW && !portalRunning)
  {
    portalRunning = true;

    SerialDebug.println("Starting Config Portal");
    wifiManager.startConfigPortal();
  }
}

void mqttEnsureConnected()
{
  if (mqttClient.connect(BOARD_IDENTIFIER, mqtt_username, mqtt_password, MQTT_TOPIC_STATUS, 1, true, AVAILABILITY_OFFLINE))
  {
    mqttClient.subscribe(MQTT_TOPIC_COMMAND);
    mqttClient.publish(MQTT_TOPIC_STATUS, AVAILABILITY_ONLINE, true);
  }
  else
  {
    SerialDebug.println("Unable to connect to MQTT broker");
  }
}

void setupMQTT()
{
  mqttClient.setClient(wifiClient);

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setKeepAlive(10);
  mqttClient.setBufferSize(2048);
  mqttClient.setCallback(mqttCallback);

  mqttEnsureConnected();
}

void loopMDNS()
{
  MDNS.update();
}

void loopMQTT()
{
  if (currentTime - stateUpdatePreviousMillis > MQTT_STATUS_TIMEOUT)
  {
    publishState();
  }

  mqttClient.loop();
  if (mqttClient.connected())
    return;
  if (currentTime - lastMqttConnectionAttempt < MQTT_CONN_RETRY_INTERVAL)
    return;

  SerialDebug.println("Connection to MQTT lost, reconnecting...");
  lastMqttConnectionAttempt = currentTime;

  mqttEnsureConnected();
}

boolean shouldUpdateState = false;

void clearDownstreamQueueAtIndex(int index)
{
  memset(downstreamQueue[index], 0, sizeof(downstreamQueue[index]));
}

void queueDownstreamMessage(char *message)
{
  if (downstreamQueueIndex >= DOWNSTREAM_QUEUE_SIZE - 1)
  {
    SerialDebug.printf("Error: Queue is full. Dropping message: <%s>\n", message);
    return;
  }

  downstreamQueueIndex++;
  snprintf(downstreamQueue[downstreamQueueIndex], sizeof(downstreamQueue[downstreamQueueIndex]), "%s", message);
}

void fillNextDownstreamMessage()
{
  memset(nextDownstreamMessage, 0, sizeof(nextDownstreamMessage));

  if (downstreamQueueIndex < 0)
  {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, "none");
  }
  else if (downstreamQueueIndex == 0)
  {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[0]);
    clearDownstreamQueueAtIndex(0);
    downstreamQueueIndex--;
  }
  else
  {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[0]);
    for (int i = 0; i < downstreamQueueIndex; i++)
    {
      snprintf(downstreamQueue[i], DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[i + 1]);
    }

    clearDownstreamQueueAtIndex(downstreamQueueIndex);
    downstreamQueueIndex--;
  }
}

void loopUART()
{
  if (!Serial.available())
  {
    return;
  }

  memset(serialRxBuf, 0, sizeof(serialRxBuf));
  Serial.readBytesUntil('\r', serialRxBuf, 250);

  if (DebugEnabled)
  {
    mqttClient.publish(MQTT_TOPIC_DEBUG, serialRxBuf, true);
  }

  SerialDebug.printf("UART says: <%s>\n", serialRxBuf);

  if (strncmp(serialRxBuf, "properties_changed", 18) == 0)
  {
    int propSiid = 0;
    int propPiid = 0;
    char propValue[6] = "";

    int propChanged = sscanf(serialRxBuf, "properties_changed %d %d %s", &propSiid, &propPiid, propValue);

    if (propChanged == 3)
    {
      SerialDebug.printf("Property changed: %d %d %s\n", propSiid, propPiid, propValue);

      if (propSiid == PROP_POWER_SIID && propPiid == PROP_POWER_PIID)
      {
        state.powerOn = strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New power status: <%s>\n", state.powerOn ? "on" : "off");
      }
      else if (propSiid == PROP_SET_SIID && propPiid == PROP_HUMIDITY_MODE_PIID)
      {
        state.mode = (humMode_t)atoi(propValue);
        SerialDebug.printf("New humidityMode: <%d>\n", state.mode);
      }
      else if (propSiid == PROP_SET_SIID && propPiid == PROP_HUMIDITY_SETPOINT_PIID)
      {
        state.humiditySetpoint = atoi(propValue);
        SerialDebug.printf("New humiditySetpoint: <%d>\n", state.humiditySetpoint);
      }
      else if (propSiid == PROP_READ_SIID && propPiid == PROP_HUMIDITY_PIID)
      {
        state.currentHumidity = atoi(propValue);
        SerialDebug.printf("New currentHumidity: <%d>\n", state.currentHumidity);
      }
      else if (propSiid == PROP_READ_SIID && propPiid == PROP_TEMPERATURE_PIID)
      {
        state.currentTemperature = atoi(propValue);
        SerialDebug.printf("New currentTemperature: <%d>\n", state.currentTemperature);
      }
      else if (propSiid == PROP_LED_SIID && propPiid == PROP_LED_ENABLED_PIID)
      {
        state.ledEnabled = strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New ledEnabled: <%s>\n", state.ledEnabled ? "true" : "false");
      }
      else if (propSiid == PROP_SOUND_SIID && propPiid == PROP_SOUND_ENABLED_PIID)
      {
        state.soundEnabled = strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New soundEnabled: <%s>\n", state.soundEnabled ? "true" : "false");
      }
      else if (propSiid == PROP_WATER_TANK_SIID && propPiid == PROP_WATER_TANK_REMOVED_PIID)
      {
        state.waterTankInstalled = !(strncmp(propValue, "true", 4) == 0);
        SerialDebug.printf("New waterTankInstalled: <%s>\n", state.waterTankInstalled ? "true" : "false");
      }
      else if (propSiid == PROP_WATER_TANK_SIID && propPiid == PROP_WATER_TANK_EMPTY_PIID)
      {
        state.waterTankEmpty = strncmp(propValue, "true", 4) == 0;
        SerialDebug.printf("New waterTankEmpty: <%s>\n", state.waterTankEmpty ? "true" : "false");
      }
      else
      {
        SerialDebug.printf("Unknown property: <%s>\n", serialRxBuf);
      }

      shouldUpdateState = true;
    }

    Serial.print("ok\r");
    return;
  }

  if (strncmp(serialRxBuf, "get_down", 8) == 0)
  {
    // This if is inside here to make sure we prevent a wave of published states;
    // if we wait until we get a "get_down" it means properties have been updated
    if (shouldUpdateState == true)
    {
      shouldUpdateState = false;
      publishState();
    }

    fillNextDownstreamMessage();

    if (strncmp(nextDownstreamMessage, "none", 4) != 0)
    {
      SerialDebug.printf("Sending: %s\n", nextDownstreamMessage);
    }

    memset(serialTxBuf, 0, sizeof(serialTxBuf));
    snprintf(serialTxBuf, sizeof(serialTxBuf), "down %s\r", nextDownstreamMessage);
    Serial.print(serialTxBuf);

    return;
  }

  if (strncmp(serialRxBuf, "net", 3) == 0)
  {
    Serial.print("cloud\r");
    return;
  }

  if (
      strncmp(serialRxBuf, "mcu_version", 11) == 0 ||
      strncmp(serialRxBuf, "model", 5) == 0 ||
      strncmp(serialRxBuf, "event_occured", 13) == 0)
  {
    Serial.print("ok\r");
    return;
  }

  if (strncmp(serialRxBuf, "result", 6) == 0)
  {
    Serial.print("ok\r");
    return;
  }

  SerialDebug.printf("UART unexpected: %s\n", serialRxBuf);
}

void queuePropertyChange(int siid, int piid, const char *value)
{
  char msg[DOWNSTREAM_QUEUE_ELEM_SIZE];
  snprintf(msg, sizeof(msg), "set_properties %d %d %s", siid, piid, value);
  queueDownstreamMessage(msg);
}

void queuePropertyChange(int siid, int piid, char *value)
{
  char msg[DOWNSTREAM_QUEUE_ELEM_SIZE];
  snprintf(msg, sizeof(msg), "set_properties %d %d %s", siid, piid, value);
  queueDownstreamMessage(msg);
}

void setPowerState(boolean powerOn)
{
  queuePropertyChange(PROP_POWER_SIID, PROP_POWER_PIID, powerOn ? "true" : "false");
}

void setLEDState(boolean ledEnabled)
{
  queuePropertyChange(PROP_LED_SIID, PROP_LED_ENABLED_PIID, ledEnabled ? "true" : "false");
}

void setSoundState(boolean soundEnabled)
{
  queuePropertyChange(PROP_SOUND_SIID, PROP_SOUND_ENABLED_PIID, soundEnabled ? "true" : "false");
}

void setHumidityMode(humMode_t mode)
{
  char modeStr[1];
  sprintf(modeStr, "%d", mode);
  queuePropertyChange(PROP_SET_SIID, PROP_HUMIDITY_MODE_PIID, modeStr);
}

void setHumiditySetpoint(uint8_t value)
{
  char valueStr[1];
  sprintf(valueStr, "%d", value > 60 ? 60 : (value < 40 ? 40 : value));
  queuePropertyChange(PROP_SET_SIID, PROP_HUMIDITY_SETPOINT_PIID, valueStr);
}

void sendNetworkStatus(boolean isConnected)
{
  queueDownstreamMessage("MIIO_net_change cloud");
}

void setupUART()
{
  Serial.begin(115200);
  Serial.swap();
  delay(1000);
  sendNetworkStatus(false);
}

void setupOTA()
{
  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.setHostname(BOARD_IDENTIFIER);
  ArduinoOTA.setPassword(BOARD_IDENTIFIER);
  ArduinoOTA.begin();
}

void loopOTA()
{
  ArduinoOTA.handle();
}

void publishState()
{
  stateUpdatePreviousMillis = currentTime;
  SerialDebug.println("Publishing new state");

  DynamicJsonDocument wifiJson(192);
  DynamicJsonDocument stateJson(604);
  char payload[256];

  wifiJson["ssid"] = WiFi.SSID();
  wifiJson["ip"] = WiFi.localIP().toString();
  wifiJson["rssi"] = WiFi.RSSI();

  stateJson["state"] = state.powerOn ? "on" : "off";

  switch (state.mode)
  {
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
  stateJson["debug"] = DebugEnabled;
  stateJson["uart"] = UARTEnabled;

  serializeJson(stateJson, payload);
  mqttClient.publish(MQTT_TOPIC_STATE, payload, true);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  char payloadText[length + 1];
  snprintf(payloadText, length + 1, "%s", payload);
  SerialDebug.printf("MQTT callback with topic <%s> and payload <%s>\n", topic, payloadText);

  if (strcmp(topic, MQTT_TOPIC_COMMAND) == 0)
  {
    DynamicJsonDocument commandJson(256);
    DeserializationError err = deserializeJson(commandJson, payloadText);

    if (err)
    {
      SerialDebug.println("Error deserializing JSON");
      return;
    }

    String stateCommand = commandJson["state"].as<String>();
    String modeCommand = commandJson["mode"].as<String>();
    String soundCommand = commandJson["sound"].as<String>();
    String ledCommand = commandJson["led"].as<String>();
    String command = commandJson["command"].as<String>();

    long humiditySetpointCommand = commandJson["humiditySetpoint"] | -1;

    if (stateCommand == "off")
    {
      setPowerState(false);
    }
    else if (stateCommand == "on")
    {
      setPowerState(true);
    }

    if (modeCommand == "low")
    {
      setHumidityMode((humMode_t)low);
    }
    else if (modeCommand == "medium")
    {
      setHumidityMode((humMode_t)medium);
    }
    else if (modeCommand == "high")
    {
      setHumidityMode((humMode_t)high);
    }
    else if (modeCommand == "setpoint")
    {
      setHumidityMode((humMode_t)setpoint);
    }

    if (soundCommand == "off")
    {
      setSoundState(false);
    }
    else if (soundCommand == "on")
    {
      setSoundState(true);
    }

    if (ledCommand == "off")
    {
      setLEDState(false);
    }
    else if (ledCommand == "on")
    {
      setLEDState(true);
    }

    if (humiditySetpointCommand > -1)
    {
      setHumiditySetpoint((uint8_t)humiditySetpointCommand);
    }

    if (command == "reboot")
    {
      ESP.restart();
    }
    else if (command == "debug")
    {
      DebugEnabled = true;
    }
    else if (command == "undebug")
    {
      DebugEnabled = false;
    }
    else if (command == "enableUART")
    {
      UARTEnabled = true;
    }
    else if (command == "disableUART")
    {
      UARTEnabled = false;
    }
    else if (command == "publishState")
    {
      publishState();
    }
  }
}

void setup()
{
  setupUART();

  setupGeneric();

  setupWifi();
  setupMDNS();
  setupOTA();

  setupMQTT();
}

void loop()
{
  currentTime = millis();

  if (UARTEnabled)
  {
    loopUART();
  }

  loopWifi();
  loopMDNS();
  loopOTA();

  loopMQTT();
}