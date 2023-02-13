#define MQTT_MAX_PACKET_SIZE 2048
#define VERSION "0.5.0"

#include <FS.h>  //this needs to be first, or it all crashes and burns...
#include <uptime_formatter.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>



#define FLASH_PIN 0
#define STATUS_PIN 16
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"

#define MQTT_TOPIC_DEBUG "esp/debug"



SoftwareSerial SerialDebug(3, 1);


unsigned long lastMqttConnectionAttempt = millis();
unsigned long stateUpdatePreviousMillis = millis();

enum humMode_t {
  unknown = -1,
  low = 1,
  medium = 2,
  high = 3,
  setpoint = 4
};

struct humidifierState_t {
  boolean powerOn;

  humMode_t mode = (humMode_t)-1;

  int humiditySetpoint = -1;

  int currentHumidity = -1;
  int currentTemperature = -1;

  boolean beepEnabled;
  boolean ledEnabled;

  boolean waterTankInstalled;
  boolean waterTankEmpty;
};


humidifierState_t state;

char serialRxBuf[255];
char serialTxBuf[255];

boolean DebugEnabled = true;
boolean UARTEnabled = true;

#define PROP_POWER_SIID 2
#define PROP_POWER_PIID 1

#define PROP_READ_SIID 3
#define PROP_HUMIDITY_PIID 1
#define PROP_TEMPERATURE_PIID 7

#define PROP_SET_SIID 2
#define PROP_HUMIDITY_MODE_PIID 5
#define PROP_HUMIDITY_SETPOINT_PIID 6

#define PROP_beep_SIID 5
#define PROP_beep_ENABLED_PIID 1

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


bool portalRunning = false;
/* my vars -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- my vars */


String buffer_msg = "";
/* new vars */

char HOSTNAME[48] = "STPM";
char LOCATION[48] = "UNKNWN";

const IPAddress apIP(192, 168, 97, 1);

boolean settingMode;
String ssidList;
char ssid[40] = "";
char pass[80] = "";

/* new vars eof */

int i = 0; int cha = 0; bool notmatch = true; int reporter_times_count = 0;
int reporter_times = 1000;  // to report mqtt=100 ( 5000 x delay_std (5) ) = 25 sec
int delay_std = 5;          // x to upside vars
// int delay_std = 1000;     // x to upside vars

bool manual_fan = true;
int light_mode = 0;

//                      VARS: WIFI
// const char* ssid = "ssid";
// const char* password = "password";

WiFiClient espClient;

PubSubClient client(espClient);  // mqtt client
long lastReconnectAttempt = 0;

//                      VARS: mqtt
// const char* mqtt_server = "m24.cloudmqtt.com";
// const int mqtt_port = 13261;
//define your default values here, if there are different values in config.json, they are overwritten.

char mqtt_server[40];
char mqtt_port[6] = "1883";

bool mqtt_auth = true;
char mqtt_user[40];
char mqtt_password[40];
bool lastmqttpublishstatereport = false;
char buffer[2048];
// int mqtt_try = 0;


typedef struct TOPIC {
  String name;
  String pub;
  String sub;
  String state;
};
TOPIC topic[99];
int topic_i = 0;



String IpAddress2String(const IPAddress& ipAddress) {
  return String(ipAddress[0]) + String(".") + String(ipAddress[1]) + String(".") + String(ipAddress[2]) + String(".") + String(ipAddress[3]);
}


const char* fan_topic_state_s = "esp/airpump/state/fan";  // fixme

const char* mqtt_community_emerg_s = "esp/msg/emerge";
const char* mqtt_commnunity_s = "esp/msg/community";
const char* mqtt_incoming_topic_s = "esp/airpump/cmd/reboot";
const char* mqtt_commnunity_s_msgs = "esp/msg/airpump_msg";

String mqtt_sub_prefix = String (String("esp/") + String(HOSTNAME) + "/" + String(LOCATION));

String debug_topic_switch_s       = String(mqtt_sub_prefix + "/debug").c_str();

String power_topic_switch_s      = String(mqtt_sub_prefix + "/switch/power").c_str();
String led_topic_switch_s        = String(mqtt_sub_prefix + "/switch/led").c_str();
String status_led_topic_switch_s = String(mqtt_sub_prefix + "/switch/status_led").c_str();
String beep_topic_switch_s       = String(mqtt_sub_prefix + "/switch/beep").c_str();
String fan_topic_switch_s        = String(mqtt_sub_prefix + "/switch/fan").c_str();
String mode_topic_switch_s       = String(mqtt_sub_prefix + "/switch/mode").c_str();
String hum_topic_switch_s        = String(mqtt_sub_prefix + "/switch/hum").c_str();

const char* led_topic_state_s = "esp/airpump/state/led1";
const char* fan_topic_json_s = "esp/airpump/json";

const char* fan_topic_switch_all_s = "esp/fan_all/switch/fan";
const char* reboot_topic_switch_s = "esp/airpump/switch/reboot";
const char* manual_topic_switch_s = "esp/airpump/switch/manual";
/*
const char*        dht22_topic_temp_s = "esp/airpump/state/dht22_temp";
const char*        dht22_topic_hum_s  = "esp/airpump/state/dht22_hum";
const char*        dht22_topic_temp_avg_s = "esp/airpump/state/dht22_temp_avg";
const char*        dht22_topic_hum_avg_s  = "esp/airpump/state/dht22_hum_avg";
const char*        dht22_topic_errmil_s = "esp/airpump/state/dht22_errmils";
const char*        dht22_topic_count_s = "esp/airpump/state/dht22_errcount";
const char*        dht22_topic_total_count_s = "esp/airpump/state/dht22_total_errcount";
const char*        dht22_topic_curmil_s = "esp/airpump/state/dht22_curmils";
const char*        dht22_topic_state_s = "esp/airpump/state/dht22_status";

/*
*/

void setupPins() {
  pinMode(STATUS_PIN, OUTPUT);
  digitalWrite(STATUS_PIN, HIGH);
  pinMode(FLASH_PIN, INPUT_PULLUP);
}


boolean shouldUpdateState = false;

void clearDownstreamQueueAtIndex(int index) {
  memset(downstreamQueue[index], 0, sizeof(downstreamQueue[index]));
}

void queueDownstreamMessage(char* message) {
  if (downstreamQueueIndex >= DOWNSTREAM_QUEUE_SIZE - 1) {
//    SerialDebug.printf("Error: Queue is full. Dropping message: <%s>\n", message);
      client.publish(MQTT_TOPIC_DEBUG, "Queue is full droping message");
    return;
  }

  downstreamQueueIndex++;
  snprintf(downstreamQueue[downstreamQueueIndex], sizeof(downstreamQueue[downstreamQueueIndex]), "%s", message);
  client.publish(MQTT_TOPIC_DEBUG, String(String("Message is queued:[")+String(message)+String("]")).c_str());
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

void loopUART() {
  if (!Serial.available()) {
    // client.publish(MQTT_TOPIC_DEBUG, "Serial is not available");
    return;
  }

  memset(serialRxBuf, 0, sizeof(serialRxBuf));
  Serial.readBytesUntil('\r', serialRxBuf, 250);

  if (DebugEnabled) {
    if (String("get_down") != String(serialRxBuf)) {
    client.publish(MQTT_TOPIC_DEBUG, String(String("Serial rxbuff: ") + String(serialRxBuf)).c_str());
    }
  }

  if (strncmp(serialRxBuf, "properties_changed", 18) == 0) {
    int propSiid = 0;
    int propPiid = 0;
    char propValue[6] = "";

    int propChanged = sscanf(serialRxBuf, "properties_changed %d %d %s", &propSiid, &propPiid, propValue);

    if (propChanged == 3) {
     // SerialDebug.printf("Property changed: %d %d %s\n", propSiid, propPiid, propValue);
      // client.publish(MQTT_TOPIC_DEBUG, "Property changed");
      if (propSiid == PROP_POWER_SIID && propPiid == PROP_POWER_PIID) {
        state.powerOn = strncmp(propValue, "true", 4) == 0;
      // SerialDebug.printf("New power status: <%s>\n", state.powerOn ? "on" : "off");
      } else if (propSiid == PROP_SET_SIID && propPiid == PROP_HUMIDITY_MODE_PIID) {
        state.mode = (humMode_t)atoi(propValue);
      // SerialDebug.printf("New humidityMode: <%d>\n", state.mode);
      } else if (propSiid == PROP_SET_SIID && propPiid == PROP_HUMIDITY_SETPOINT_PIID) {
        state.humiditySetpoint = atoi(propValue);
      // SerialDebug.printf("New humiditySetpoint: <%d>\n", state.humiditySetpoint);
      } else if (propSiid == PROP_READ_SIID && propPiid == PROP_HUMIDITY_PIID) {
        state.currentHumidity = atoi(propValue);
      //  SerialDebug.printf("New currentHumidity: <%d>\n", state.currentHumidity);
      } else if (propSiid == PROP_READ_SIID && propPiid == PROP_TEMPERATURE_PIID) {
        state.currentTemperature = atoi(propValue);
      //  SerialDebug.printf("New currentTemperature: <%d>\n", state.currentTemperature);
      } else if (propSiid == PROP_LED_SIID && propPiid == PROP_LED_ENABLED_PIID) {
        state.ledEnabled = strncmp(propValue, "true", 4) == 0;
      //  SerialDebug.printf("New ledEnabled: <%s>\n", state.ledEnabled ? "true" : "false");
      } else if (propSiid == PROP_beep_SIID && propPiid == PROP_beep_ENABLED_PIID) {
        state.beepEnabled = strncmp(propValue, "true", 4) == 0;
      //  SerialDebug.printf("New beepEnabled: <%s>\n", state.beepEnabled ? "true" : "false");
      } else if (propSiid == PROP_WATER_TANK_SIID && propPiid == PROP_WATER_TANK_REMOVED_PIID) {
        state.waterTankInstalled = !(strncmp(propValue, "true", 4) == 0);
      //  SerialDebug.printf("New waterTankInstalled: <%s>\n", state.waterTankInstalled ? "true" : "false");
      } else if (propSiid == PROP_WATER_TANK_SIID && propPiid == PROP_WATER_TANK_EMPTY_PIID) {
        state.waterTankEmpty = strncmp(propValue, "true", 4) == 0;
      //  SerialDebug.printf("New waterTankEmpty: <%s>\n", state.waterTankEmpty ? "true" : "false");
      } else {
        client.publish(MQTT_TOPIC_DEBUG, String(String("Unknown property:")+String(serialRxBuf)).c_str());
      //  SerialDebug.printf("Unknown property: <%s>\n", serialRxBuf);
      }

      shouldUpdateState = true;
    }

    Serial.print("ok\r");
    return;
  }

  if (strncmp(serialRxBuf, "get_down", 8) == 0) {
    // This if is inside here to make sure we prevent a wave of published states;
    // if we wait until we get a "get_down" it means properties have been updated
    if (shouldUpdateState == true) {
      shouldUpdateState = false;
      reporter_now();
    }

    fillNextDownstreamMessage();

    if (strncmp(nextDownstreamMessage, "none", 4) != 0) {
    client.publish(MQTT_TOPIC_DEBUG, String(String("sending:[")+String(nextDownstreamMessage)+String("]")).c_str());
//      SerialDebug.printf("Sending: %s\n", nextDownstreamMessage);
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
    strncmp(serialRxBuf, "mcu_version", 11) == 0 || strncmp(serialRxBuf, "model", 5) == 0 || strncmp(serialRxBuf, "event_occured", 13) == 0) {
    Serial.print("ok\r");
    return;
  }

  if (strncmp(serialRxBuf, "result", 6) == 0) {
    Serial.print("ok\r");
    return;
  }

//  SerialDebug.printf("UART unexpected: %s\n", serialRxBuf);
}


void queuePropertyChange(int siid, int piid, const char* value) {
  char msg[DOWNSTREAM_QUEUE_ELEM_SIZE];
  snprintf(msg, sizeof(msg), "set_properties %d %d %s", siid, piid, value);
  queueDownstreamMessage(msg);
}

void queuePropertyChange(int siid, int piid, char* value) {
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

void setbeepState(boolean beepEnabled) {
  queuePropertyChange(PROP_beep_SIID, PROP_beep_ENABLED_PIID, beepEnabled ? "true" : "false");
}

void setHumidityMode(humMode_t mode) {
  char modeStr[1];
  sprintf(modeStr, "%d", mode);
  queuePropertyChange(PROP_SET_SIID, PROP_HUMIDITY_MODE_PIID, modeStr);
}

void setHumiditySetpoint(uint8_t value) {
  char valueStr[1];
    sprintf(valueStr, "%d", value > 60 ? 60 : (value < 40 ? 40 : value));
  // sprintf(valueStr, "%d", value > 90 ? 90 : (value < 10 ? 10 : value));
  queuePropertyChange(PROP_SET_SIID, PROP_HUMIDITY_SETPOINT_PIID, valueStr);
}

void sendNetworkStatus(boolean isConnected) {
  queueDownstreamMessage("MIIO_net_change cloud");
}

void setupUART() {
  Serial.begin(115200);
  Serial.swap();
  delay(2000);
  sendNetworkStatus(false);
}


/*
*/
bool read_config() {
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if (!deserializeError) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          Serial.println("\nparsed json");
          if (json['debug'] == 1 ) { DebugEnabled = true;} else { DebugEnabled = false; };
          strcpy(ssid, String(json["ssid"]).c_str());
          strcpy(pass, String(json["pass"]).c_str());
          strcpy(mqtt_server, String(json["mqtt_server"]).c_str());
          strcpy(mqtt_port, String(json["mqtt_port"]).c_str());
          //          strcpy(mqtt_auth, json["mqtt_auth"]);
          strcpy(mqtt_user, String(json["mqtt_user"]).c_str());
          strcpy(mqtt_password, String(json["mqtt_password"]).c_str());
          strcpy(HOSTNAME, String(json["HOSTNAME"]).c_str());
          strcpy(LOCATION, String(json["LOCATION"]).c_str());
        } else {
          Serial.println("failed to load json config");
          return false;
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS, will format SPIFFS");
  }
  return true;
}

bool save_config() {
  Serial.println("saving config");
#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
  DynamicJsonDocument json(1024);
#else
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
#endif
  if (DebugEnabled) { json["debug"] = 1; } else { DebugEnabled = 0; };
  json["ssid"] = ssid;
  json["pass"] = pass;
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  if (mqtt_auth) {
    json['mqtt_auth'] = 1;
  } else {
    json['mqtt_auth'] = 0;
  };
  json["mqtt_user"] = mqtt_user;
  json["mqtt_password"] = mqtt_password;
  json["HOSTNAME"] = HOSTNAME;
  json["LOCATION"] = LOCATION;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
  serializeJson(json, Serial);
  serializeJson(json, configFile);
#else
  json.printTo(Serial);
  json.printTo(configFile);
#endif
  configFile.close();
  //end save
  return true;
}


void setup_topics() {
  setup_topic("commands", "", String(String("json/commands/airpump/") + String(LOCATION)), "");
}

void setup_topic(String name, String pub, String sub, String state) {
  topic[topic_i] = (TOPIC){
    name, pub, sub, state
  };
  topic_i++;
}


long debounce = 600;  // the debounce time, increase if the output flickers
int fanspeed = 0;
bool light = false;


boolean restoreConfig() {
  strcpy(HOSTNAME, "");
  strcpy(LOCATION, "");
  if (read_config()) {
    WiFi.hostname(HOSTNAME);
    WiFi.softAPdisconnect(true);
    WiFi.begin(ssid, pass);
    return true;
  } else {
    return false;
  }
}

void ota_init() {
  WiFi.setAutoReconnect(true);
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_SPIFFS
      type = "filesystem";
    }
  });
  ArduinoOTA.onEnd([]() {

  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // log_msg(String("Progress: " + (progress / (total / 100))));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    // log_err(String("Error: " + error));
    if (error == OTA_AUTH_ERROR) {
      // log_err(String("Auth Failed"));
    } else if (error == OTA_BEGIN_ERROR) {
      //      log_err(String("Begin Failed"));
    } else if (error == OTA_CONNECT_ERROR) {
      //  log_err(String("Connect Failed"));
    } else if (error == OTA_RECEIVE_ERROR) {
      //  log_err(String("Receive Failed"));
    } else if (error == OTA_END_ERROR) {
      //  log_err(String("End Failed"));
    }
  });
  ArduinoOTA.begin();
}


void mqttcallback(char* topic, unsigned char* payload, unsigned int lenght) {  // fixme to json
int bufflenght = 32;
char buffer4[bufflenght]; // <-- Enough room for both strings and a NULL character
for (int i = 0; (i< lenght && i< bufflenght); i++)
{
 buffer4[i] = payload[i];
}

  buffer_msg = "Callback called\n";
  String stopic = String(topic);
  int val = payload[0];
  buffer_msg = +"topic is: [" + stopic + "] payload ["+ val +"]\n";
  char* top = topic;
  notmatch = false;
  if (stopic == debug_topic_switch_s) { if ( val == 48 ) { DebugEnabled = false; }; if ( val == 49 ) { DebugEnabled = true; }; return; };
  if (stopic == power_topic_switch_s) { if ( val == 48 ) { setPowerState(false); }; if ( val == 49 ) { setPowerState(true); }; return; };
  if (stopic == led_topic_switch_s) {   if ( val == 48 ) { setLEDState(false); };   if ( val == 49 ) { setLEDState(true); };   return; };
  if (stopic == beep_topic_switch_s) {  if ( val == 48 ) { setbeepState(false); };  if ( val == 49 ) { setbeepState(true); };  return; };
  if (stopic == fan_topic_switch_s) {  if ( val == 48 ) {  setPowerState(false); } if (val == 49) { setPowerState(true); setHumidityMode((humMode_t)low); };  if ( val == 50 ) {  setPowerState(true); setHumidityMode((humMode_t)medium); }; if ( val == 51 ) {  setPowerState(true); setHumidityMode((humMode_t)high); }; if ( val == 52 ) {  setPowerState(true); setHumidityMode((humMode_t)setpoint); }; return; };
  if (stopic == hum_topic_switch_s) {
     String dbgmsg = String(String("Set target huminidity to:") + String((uint8_t) atoi(buffer4) )); client.publish(MQTT_TOPIC_DEBUG,dbgmsg.c_str());
     setHumiditySetpoint((uint8_t)atoi(buffer4)); };
  return;
}



DNSServer dnsServer;
ESP8266WebServer webServer(80);


void setupMode() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  delay(100);
  Serial.println("");
  for (int i = 0; i < n; ++i) {
    ssidList += "<option value=\"";
    ssidList += WiFi.SSID(i);
    ssidList += "\">";
    ssidList += WiFi.SSID(i);
    ssidList += "</option>";
  }
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(HOSTNAME);
  dnsServer.start(53, "*", apIP);
  startWebServer();
  Serial.print("Starting Access Point at \"");
  Serial.print(HOSTNAME);
  Serial.println("\"");
}

void updateMqttCommunity() {
mqtt_sub_prefix = String (String("esp/") + String(HOSTNAME) + "/" + String(LOCATION));
power_topic_switch_s      = String(mqtt_sub_prefix + "/switch/power").c_str();
led_topic_switch_s        = String(mqtt_sub_prefix + "/switch/led").c_str();
status_led_topic_switch_s = String(mqtt_sub_prefix + "/switch/status_led").c_str();
beep_topic_switch_s       = String(mqtt_sub_prefix + "/switch/beep").c_str();
fan_topic_switch_s        = String(mqtt_sub_prefix + "/switch/fan").c_str();
hum_topic_switch_s        = String(mqtt_sub_prefix + "/switch/hum").c_str();
mode_topic_switch_s       = String(mqtt_sub_prefix + "/switch/mode").c_str();
}

void setup() {
  setupPins();
  setupUART();
  // Serial.begin(115200);
  /*
  buffer_msg += "Startup...\r\n";
  digitalWrite(DHT22_PIN, LOW);
  setup_switch(SWITCH1);
  delay(100);
  setup_switch(SWITCH2);
  delay(100);
  setup_switch(SWITCH3);
  delay(100);
  setup_switch(SWITCH4);
  delay(100);

  setup_button(BUTTON1);
  setup_button(BUTTON2);
  setup_button(BUTTON3);
  setup_button(BUTTON4);
  setup_button(BUTTON5);

  delay(2000);
  digitalWrite(DHT22_PIN, HIGH);
*/

  buffer_msg += "Trying to restore config...\r\n";
  //Serial.println(buffer_msg);
  if (restoreConfig()) {
    buffer_msg += "Config restored...\r\n";
//    fan_topic_switch_s = String("esp/" + String(HOSTNAME) + "/" + String(LOCATION) + "/switch/fan").c_str();
    led_topic_switch_s = String("esp/" + String(HOSTNAME) + "/" + String(LOCATION) + "/switch/led").c_str();
    buffer_msg += "Checking connection";
    if (checkConnection()) {
      buffer_msg += "Connection ok...";
      updateMqttCommunity();
      ota_init();
      client.setBufferSize(2048);

      client.setServer(mqtt_server, String(mqtt_port).toInt());
      client.setCallback(mqttcallback);

      char charBuf[50];
      client.connect(HOSTNAME);
      delay(1500);
      lastReconnectAttempt = 0;
      mqtt_reconnect();
      settingMode = false;
      startWebServer();
      return;
    }
  }
  buffer_msg += "Connection is not ok...";
  buffer_msg += "Config not restored...\r\n";
  settingMode = true;
  strcpy(HOSTNAME, "SETUPME");
  setupMode();
  ota_init();
}





bool mqtt_reconnect() {
  buffer_msg += "Calling mqtt_reconnect...\r\n";
  if (client.connect(HOSTNAME)) {
    client.subscribe(debug_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(debug_topic_switch_s)).c_str()); }
    client.subscribe(reboot_topic_switch_s);
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(reboot_topic_switch_s)).c_str()); }
    client.subscribe(power_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(power_topic_switch_s)).c_str()); }
    client.subscribe(led_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(led_topic_switch_s)).c_str()); }
    client.subscribe(status_led_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(status_led_topic_switch_s)).c_str()); }
    client.subscribe(beep_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(beep_topic_switch_s)).c_str()); }
    client.subscribe(fan_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(fan_topic_switch_s)).c_str()); }
    client.subscribe(mode_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(mode_topic_switch_s)).c_str()); }
    client.subscribe(hum_topic_switch_s.c_str());
    if (DebugEnabled) { client.publish(MQTT_TOPIC_DEBUG, String(String("MQTT Subscribe: ") + String(hum_topic_switch_s)).c_str()); }
    client.setCallback(mqttcallback);

    buffer_msg += "[mqtt_reconnect()]: Connected, finding community for subscribe...\n";
    for (int i = 0; i < topic_i; i++) {
      if (topic[i].sub != String("")) {
        client.subscribe(topic[i].sub.c_str());
        buffer_msg += "[mqtt_reconnect()]::subscribe subscribing to: " + topic[i].sub + "\n";
      }
    };
  };
  return client.connected();
}


void reporter_now() {
  DynamicJsonDocument doc(2048);

  doc["device"] = HOSTNAME;
  doc["name"] = HOSTNAME;
  doc["device_ip"] = IpAddress2String(WiFi.localIP());
  doc["location"] = LOCATION;
  doc["millis"] = millis();
  doc["version"] = VERSION;
  doc["rssi"] = WiFi.RSSI();
  if (state.humiditySetpoint != -1) {
  doc["state"] = state.powerOn ? "on" : "off";
  doc["state_fool"] = state.powerOn ? "4" : "0";
  if (state.powerOn) {
  switch (state.mode) {
    case (humMode_t)low: doc["mode"] = "low"; doc["fan"] = 1; break;
    case (humMode_t)medium: doc["mode"] = "medium"; doc["fan"] = 2; break;
    case (humMode_t)high: doc["mode"] = "high"; doc["fan"] = 3; break;
    case (humMode_t)setpoint: doc["mode"] = "setpoint"; doc["fan"] = 4; break;
    default: doc["mode"] = "unknown"; doc["fan"] = 0; break;
  }
  } else {
    doc["mode"] = "off"; doc["fan"] = 0;
  }
  doc["humiditySetpoint"] = state.humiditySetpoint;
  doc["humidity"] = state.currentHumidity;
  doc["temperature"] = state.currentTemperature;
  doc["beep"] = state.beepEnabled ? "on" : "off"; doc["beep_bool"] = state.beepEnabled ? "1" : "0";
  doc["led"] = state.ledEnabled ? "on" : "off"; doc["led_bool"] = state.ledEnabled ? "1" : "0";
  doc["waterTank"] = state.waterTankInstalled ? (state.waterTankEmpty ? "empty" : "full") : "missing";
  doc["waterTank_ok"] = state.waterTankInstalled ? (state.waterTankEmpty ? "0" : "1") : "0";
  doc["waterTank_problem"] = state.waterTankInstalled ? (state.waterTankEmpty ? "1" : "0") : "1";
  }
  size_t n = serializeJson(doc, buffer);
  lastmqttpublishstatereport = client.publish(String(String("json/sensors/"+String(HOSTNAME)+"/") + String(LOCATION)).c_str(), buffer, n);
  reporter_times_count = 0;
}


void reporter() {
  if (reporter_times_count <= reporter_times) {
    reporter_times_count++;
  } else {
    /*
    DynamicJsonDocument doc(2048);

    doc["device"] = HOSTNAME;
    doc["name"] = HOSTNAME;
    doc["device_ip"] = IpAddress2String(WiFi.localIP());
    doc["location"] = LOCATION;
    //  doc["light"] = get_state(SWITCH4);

    doc["millis"] = millis();
    doc["version"] = VERSION;
    doc["rssi"] = WiFi.RSSI();
    doc["state"] = state.powerOn ? "on" : "off";
    doc["state_bool"] = state.powerOn ? "1" : "0";

    switch (state.mode) {
      case (humMode_t)setpoint: doc["mode"] = "setpoint"; break;
      case (humMode_t)low: doc["mode"] = "low";       doc["fan"] = "1"; break;
      case (humMode_t)medium: doc["mode"] = "medium"; doc["fan"] = "2"; break;
      case (humMode_t)high: doc["mode"] = "high";     doc["fan"] = "3";  break;
      default: doc["mode"] = "unknown"; doc["fan"] = "0"; break;
    }

    doc["humiditySetpoint"] = state.humiditySetpoint;
    doc["humidity"] = state.currentHumidity;
    doc["temperature"] = state.currentTemperature;
    doc["beep"] = state.beepEnabled ? "on" : "off"; doc["beep_bool"] = state.beepEnabled ? "1" : "0";
    doc["led"] = state.ledEnabled ? "on" : "off"; doc["led_bool"] = state.ledEnabled ? "1" : "0";
    doc["waterTank"] = state.waterTankInstalled ? (state.waterTankEmpty ? "empty" : "full") : "missing";
    doc["waterTank_ok"] = state.waterTankInstalled ? (state.waterTankEmpty ? "0" : "1") : "0";

    size_t n = serializeJson(doc, buffer);
    lastmqttpublishstatereport = client.publish(String(String("json/sensors/"+String(HOSTNAME)+"/") + String(LOCATION)).c_str(), buffer, n);
    */
    reporter_now();
    reporter_times_count = 0;
  }
  delay(5);
}




boolean checkConnection() {
  int count = 0;
  // Serial.print("Waiting for Wi-Fi connection");
  while (count < 30) {
    if (WiFi.status() == WL_CONNECTED) {
      // Serial.println();
      // Serial.println("Connected!");
      return (true);
    }
    delay(500);
    // Serial.print(".");
    count++;
  }
  // Serial.println("Timed out.");
  return false;
}


void startWebServer() {

  webServer.on("/", []() {  // main page
    String s = String(
      String("<h1>STA mode: ") + HOSTNAME);
    s += " version (";
    s += VERSION;
    s += ") Located at (";
    s += LOCATION;
    s += ")";
    webServer.send(200, "text/html", makePage(String("Main page").c_str(), s));
  });


  webServer.on("/reboot", []() {
    String s = "Device will reboot shortly";
    webServer.send(200, "text/html", makePage("Reboot ESP", s));
    ESP.restart();
  });

  if (settingMode) {  // settings for wifi not working....
    //Serial.print("Starting Web Server at ");
    // Serial.println(WiFi.softAPIP());


    webServer.on("/reset", []() {
      String s;
      SPIFFS.format();
      webServer.send(200, "text/html", makePage("Reset Wi-Fi Settings", s));
      delay(2000);
      ESP.restart();
    });

    webServer.on("/settings", []() {
      String s = "<h1>Wi-Fi Settings</h1><p>Please enter your password by selecting the SSID.</p>";
      s += "<form method=\"get\" action=\"setap\">Hostname: <input name=\"hostname\" length=64 type=\"text\"><br>Location: <input name=\"location\" length=64 type=\"text\"><br><label>SSID: </label><select name=\"ssid\">";
      s += ssidList;
      s += "</select><br>Password: <input name=\"pass\" length=64 type=\"password\"><br>Debug: <input name=\"debug\" value=\"0\"><br><input type=\"submit\"></form>";
      webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
    });


    webServer.on("/setap", []() {
      String s;
      StaticJsonDocument<200> docw;
      // LittleFS.remove("/config.json");
      strcpy(ssid, urlDecode(webServer.arg("ssid")).c_str());
      strcpy(pass, urlDecode(webServer.arg("pass")).c_str());
      strcpy(HOSTNAME, urlDecode(webServer.arg("hostname")).c_str());
      strcpy(LOCATION, urlDecode(webServer.arg("location")).c_str());
      if ( urlDecode(webServer.arg("location")).c_str() == "1") { DebugEnabled = true; } else { DebugEnabled = false; };
      if (save_config()) {
        s = "<h1>Setup complete.</h1><p>device will be connected to \"";
        s += ssid;
        s += "\" after the restart.";
      } else {
        s = "<h1>Setup failed.</h1>";
      }
      webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
      delay(2000);
      ESP.restart();
    });

    webServer.onNotFound([]() {
      String s = "<h1>Takeover page</h1><p><a href=\"http";
      s += IpAddress2String(WiFi.localIP());
      s += "/?";
      s += String(millis());
      s += "\">go to main...</a></p>";
      webServer.send(200, "text/html", makePage("AP mode", s));
    });
  } else {  // settings for wifi is working...
    // Serial.print("Starting Web Server at ");
    // Serial.println(WiFi.localIP());
    webServer.on("/", []() {  // main page
      String s = "<h1>STA mode</h1><p><a href=\"/reset\">Reset Wi-Fi Settings</a></p>";
      webServer.send(200, "text/html", makePage("STA mode", s));
    });


    webServer.on("/debug", []() {
      String s = "\n<script> $( document ).ready(function() { var jsonStr = $(\"#jsonbuffer\").text(); var jsonObj = JSON.parse(jsonStr); var jsonPretty = JSON.stringify(jsonObj, null, '\t'); $(\"#jsonbuffer\").text(jsonPretty); });</script>\n";
      s += "mqtt json buffer is:<br><pre id=\"jsonbuffer\">\n";
      s += String(buffer);
      s += "\n</pre>\n\n";
      s += "Mqtt Server: " + String(mqtt_server) + " \n<br>";
      s += "Mqtt Port: " + String(mqtt_port) + " \n<br>";
      s += "Mqtt Auth: " + String(mqtt_auth) + " \n<br>";
      s += "Mqtt User: " + String(mqtt_user) + " \n<br>";
      s += "Mqtt Password: " + String(mqtt_password) + " \n\n\n\n<br><br><br>";

      s += "ssid: " + String(ssid) + " \n<br>";
      s += "pass: " + String(pass) + " \n<br>";
      s += "HOSTNAME: " + String(HOSTNAME) + " \n<br>";
      s += "LOCATION: " + String(LOCATION) + " \n<br>";

      webServer.send(200, "text/html", makePage("Debug info", s));
    });


    webServer.on("/debug-log", []() {
      String s = "\n<script> $( document ).ready(function() { var jsonStr = $(\"#jsonbuffer\").text(); var jsonObj = JSON.parse(jsonStr); var jsonPretty = JSON.stringify(jsonObj, null, '\t'); $(\"#jsonbuffer\").text(jsonPretty); });</script>\n";
      s += "Log buffer is:<br><pre id=\"jsonbuffer\">\n";
      s += String(buffer_msg);
      s += "<a href=\"/debug-log-clear\">Clear debug log</a>";

      webServer.send(200, "text/html", makePage("Debug log", s));
    });


    webServer.on("/debug-log-clear", []() {
      String s = "Log cleared";
      buffer_msg = "";
      webServer.send(200, "text/html", makePage("Debug log cleared", s));
    });


    webServer.on("/switch-led", []() {
      String s = "<form method=\"get\" action=\"switch-led-do\">\n";
      s += "<input name=\"mode\" length=64 type=\"text\" value=\"0\"><input type=submit></form><br>\n";
      webServer.send(200, "text/html", makePage("led switch", s));
    });


    webServer.on("/switch-fan", []() {
      String s = "<form method=\"get\" action=\"switch-fan-do\">\n";
      s += "<select name=\"mode\"><option value=0>Off</option><option value=1>Speed 1</option><option value=2>Speed 2</option><option value=3>Speed 3</option></select><input type=submit></form><br>\n";
      webServer.send(200, "text/html", makePage("Fan switch", s));
    });


    webServer.on("/settings", []() {
      String s = "<h1>Wi-Fi Settings</h1><p>Please enter your password by selecting the SSID.</p>";
      s += "<form method=\"get\" action=\"settings_do\">\n";
      s += "Hostname: <input name=\"hostname\" length=64 type=\"text\" value=\"" + String(HOSTNAME) + "\"><br>\n";
      s += "Location: <input name=\"location\" length=64 type=\"text\" value=\"" + String(LOCATION) + "\"><br>\n";
      s += "SSID:<input name=\"ssid\" value=\"" + String(ssid) + "\"><br>\n";
      s += "Password: <input name=\"pass\" length=64 type=\"text\" value=\"" + String(pass) + "\"><br><br>\n\n";

      s += "Mqtt Server: <input name=\"mqtt_server\" length=64 type=\"text\" value=\"" + String(mqtt_server) + "\"><br>\n";
      s += "Mqtt Port: <input name=\"mqtt_port\" length=64 type=\"text\" value=\"" + String(mqtt_port) + "\"><br>\n";
      s += "Mqtt Auth:<input name=\"mqtt_auth\" value=\"" + String(mqtt_auth) + "\"><br>\n";
      s += "Mqtt User: <input name=\"mqtt_user\" length=64 type=\"text\" value=\"" + String(mqtt_user) + "\"><br>\n";
      s += "Mqtt Password: <input name=\"mqtt_password\" length=64 type=\"text\" value=\"" + String(mqtt_password) + "\">";
      s += "<br>Debug: <input name=\"debug\" value=\"";
      if (DebugEnabled) { s += "1"; } else { s += "0"; };
      s += "\"><br><input type=\"submit\"><br>\n";

      s += "</form>";
      webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
    });

    webServer.on("/settings_do", []() {
      // 0-32 (32) - ssid
      // 32-96 (64) - password
      // 96-128 (32) - hostname
      // 128-160 (32) - location
      // 160-192 (32) - mqtt server
      // 192-200 (8) - mqtt port
      // 200-208 (8) - mqtt_auth
      // 208-230 (32) - mqtt_user
      // 230 - 262 (32) - mqtt_password

      strcpy(ssid, urlDecode(webServer.arg("ssid")).c_str());
      strcpy(pass, urlDecode(webServer.arg("pass")).c_str());
      String hostn = urlDecode(webServer.arg("hostname"));
      String locn = urlDecode(webServer.arg("location"));
      if ( String(urlDecode(webServer.arg("location"))).c_str() == "1" ) { DebugEnabled = true; } else { DebugEnabled = false; };


      strcpy(mqtt_server, urlDecode(webServer.arg("mqtt_server")).c_str());
      strcpy(mqtt_port, urlDecode(webServer.arg("mqtt_port")).c_str());
      if ("1" == urlDecode(webServer.arg("mqtt_auth")).c_str()) {
        mqtt_auth = true;
      } else {
        mqtt_auth = false;
      };
      strcpy(mqtt_user, urlDecode(webServer.arg("mqtt_user")).c_str());
      strcpy(mqtt_password, urlDecode(webServer.arg("mqtt_password")).c_str());
      String s;
      if (save_config()) {
        s = "<h1>Write config done!<br>Setup complete.</h1>";
      } else {
        s = "<h1>Write config is failed!";
      }
      webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
      delay(2000);
      ESP.restart();
    });


    webServer.on("/reset", []() {
      SPIFFS.remove("/config.json");
      String s = "<h1>Wi-Fi&mqtt settings was reset.</h1><p>Please wait 2 seconds... device now resets himself.</p>";
      webServer.send(200, "text/html", makePage("Reset Wi-Fi Settings", s));
      delay(2000);
      ESP.restart();
    });
  }
  webServer.begin();
}

// -------------------------- web functions

String makeLink(String href, String name, int type) {
  String link = "\r\n<li";
  if (type == 1) {
    link += " class=\"dropdown\"";
  };
  link += "><a href=\"";
  link += href;
  link += "?";
  link += String(millis());
  link += "\">";
  link += name;
  link += "</a>";
  if (type == 1) {
    link += "\r\n<ul class=\"sub\">";
  };
  if (type == 0) {
    link += "</li>";
  }
  return link;
}

String makeMenu() {
  String s = "<nav><ul class=\"primary\">";
  s += makeLink("/", "Main", 1);
  s += makeLink("/reboot", "Reboot", 0);
  s += "</ul></li>";
  //   s += makeLink("/get-dht22", "DHT22: Get", 0);
  s += makeLink("/debug", "Debug", 1);
  s += makeLink("/debug-log", "Log", 0);
  s += "</ul></li>";
  s += makeLink("/switch", "Droplet switch", 1);
  s += makeLink("/switch-fan", "Switch-fan", 0);
  s += makeLink("/switch-led", "Switch-led", 0);
  s += makeLink("/switch-power", "Switch-led", 0);
  s += "</ul></li>";
  s += makeLink("/settings", "Settings", 1);
  s += makeLink("/reset", "Settings: Reset", 1);
  s += "</ul></li>";
  s += "</ul></nav>";
  return s;
}

String makePage(String title, String contents) {

  String s = "<html><head><head><script src=\"https://code.jquery.com/jquery-3.6.0.min.js\"></script><meta name=\"viewport\" content=\"width=device-width\"><title>";
  s += title;
  s += "</title><style type=\"text/css\">body{background:#ccc;font-family:helvetica,arial,serif;font-size:13px;text-transform:uppercase;text-align:center}pre{text-align: left;}#content-text{text-align: left;}.wrap{display:inline-block;-webkit-box-shadow:0 0 70px #fff;-moz-box-shadow:0 0 70px #fff;box-shadow:0 0 70px #fff;margin-top:40px}.decor{background:#6EAF8D;background:-webkit-linear-gradient(left,#CDEBDB 50%,#6EAF8D 50%);background:-moz-linear-gradient(left,#CDEBDB 50%,#6EAF8D 50%);background:-o-linear-gradient(left,#CDEBDB 50%,#6EAF8D 50%);background:linear-gradient(left,white 50%,#6EAF8D 50%);background-size:50px 25%;padding:2px;display:block}a{text-decoration:none;color:#fff;display:block}ul{list-style:none;position:relative;text-align:left}li{float:left}ul:after{clear:both}ul:before,ul:after{content:\" \";display:table}#content{padding:40px; margin: 20px; margin-left: auto; margin-right: auto; background:#EBEBEB;text-align:center;letter-spacing:1px;text-shadow:1px 1px 1px #0E0E0E;-webkit-box-shadow:2px 2px 3px #888;-moz-box-shadow:2px 2px 3px #888;box-shadow:2px 2px 3px #888;border-top-right-radius:8px;border-top-left-radius:8px;border-bottom-right-radius:8px;border-bottom-left-radius:8px}nav{position:relative;background:#2B2B2B;background-image:-webkit-linear-gradient(bottom,#2B2B2B 7%,#333 100%);background-image:-moz-linear-gradient(bottom,#2B2B2B 7%,#333 100%);background-image:-o-linear-gradient(bottom,#2B2B2B 7%,#333 100%);background-image:linear-gradient(bottom,#2B2B2B 7%,#333 100%);text-align:center;letter-spacing:1px;text-shadow:1px 1px 1px #0E0E0E;-webkit-box-shadow:2px 2px 3px #888;-moz-box-shadow:2px 2px 3px #888;box-shadow:2px 2px 3px #888;border-bottom-right-radius:8px;border-bottom-left-radius:8px}ul.primary li a{display:block;padding:20px 30px;border-right:1px solid #3D3D3D}ul.primary li:last-child a{border-right:none}ul.primary li a:hover{color:#000}ul.sub{position:absolute;z-index:200;box-shadow:2px 2px 0 #BEBEBE;width:35%;display:none}ul.sub li{float:none;margin:0}ul.sub li a{border-bottom:1px dotted #ccc;border-right:none;color:#000;padding:15px 30px}ul.sub li:last-child a{border-bottom:none}ul.sub li a:hover{color:#000;background:#eee}ul.primary li:hover ul{display:block;background:#fff}ul.primary li:hover a{background:#fff;color:#666;text-shadow:none}ul.primary li:hover > a{color:#000}@media only screen and (max-width: 600px){.decor{padding:3px}.wrap{width:100%;margin-top:0}li{float:none}ul.primary li:hover a{background:none;color:#8B8B8B;text-shadow:1px 1px #000}ul.primary li:hover ul{display:block;background:#272727;color:#fff}ul.sub{display:block;position:static;box-shadow:none;width:100%}ul.sub li a{background:#272727;border:none;color:#8B8B8B}ul.sub li a:hover{color:#ccc;background:none}}</style></head><div class=\"wrap\"><span class=\"decor\"></span>";
  s += makeMenu();
  s += "</div><div id=\"content\"><center>";
  s += title;
  s += "</center><div id=\"content_text\">";
  s += contents;
  s += "</div></div><footer>Powered by ESP01S (v ";
  s += VERSION;
  s += ")<br>";
  /*  s += "(Temp Sensor: Last status:";
//  s += device_temp_status;
//  s += " last temp:";
  s += device_temp_last_temp;
  s += " last hum:";
  s += device_temp_last_hum;
  s += ")<br>";
  */
  s += "up " + uptime_formatter::getUptime() + "<br>";
  s += "Mqtt State: ";
  if (!client.connected()) {
    s += "not connected (" + String(client.state()) + ")";
    /*
-4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
-3 : MQTT_CONNECTION_LOST - the network connection was broken
-2 : MQTT_CONNECT_FAILED - the network connection failed
-1 : MQTT_DISCONNECTED - the client is disconnected cleanly
0 : MQTT_CONNECTED - the client is connected
1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect
     */
  } else {
    s += "connected (last json report state is:";
    if (lastmqttpublishstatereport) {
      s += "ok";
    } else {
      s += "fail";
    };
    s += ")[MQTT buffer size:" + String(client.getBufferSize()) + " ]";
  }
  s += "</footer></html>";

  return s;
}


String urlDecode(String input) {
  String s = input;
  s.replace("%20", " ");
  s.replace("%21", "!");
  s.replace("%22", "\"");
  s.replace("%23", "#");
  s.replace("%24", "$");
  s.replace("%25", "%");
  s.replace("%26", "&");
  s.replace("%27", "\'");
  s.replace("%28", "(");
  s.replace("%29", ")");
  s.replace("%30", "*");
  s.replace("%31", "+");
  s.replace("%2C", ",");
  s.replace("%2E", ".");
  s.replace("%2F", "/");
  s.replace("%2C", ",");
  s.replace("%3A", ":");
  s.replace("%3A", ";");
  s.replace("%3C", "<");
  s.replace("%3D", "=");
  s.replace("%3E", ">");
  s.replace("%3F", "?");
  s.replace("%40", "@");
  s.replace("%5B", "[");
  s.replace("%5C", "\\");
  s.replace("%5D", "]");
  s.replace("%5E", "^");
  s.replace("%5F", "-");
  s.replace("%60", "`");
  return s;
}



void loop() {
  ArduinoOTA.handle();
  if (settingMode) {
    dnsServer.processNextRequest();
  } else {
    if (!client.connected()) {
      long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        if (mqtt_reconnect()) {
          client.loop();
          lastReconnectAttempt = 0;
        }
      }
    } else {
      reporter();  // todo - change function to json
      client.loop();
    };
  }
  webServer.handleClient();
  if (UARTEnabled) {
    loopUART();
  }
}
