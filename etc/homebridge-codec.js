module.exports = {
  init: function ({ config }) {
    if (!config.topicPrefix) {
      throw new Error("Please set a topicPrefix");
    }
    if (!config.name) {
      throw new Error("Please set a name");
    }

    const MAP_FAN_SPEED_TO_MODE = {
      25: "low",
      50: "medium",
      75: "high",
      100: "setpoint",
    };
    const MAP_FAN_MODE_TO_SPEED = Object.entries(MAP_FAN_SPEED_TO_MODE).reduce(
      (carry, [key, value]) => {
        carry[value] = Number(key);
        return carry;
      },
      {}
    );

    const MQTT_TOPIC_WRITE = `${config.topicPrefix}/command`;
    const MQTT_TOPIC_READ = `${config.topicPrefix}/state`;
    const MQTT_TOPIC_AVAILABILITY = `${config.topicPrefix}/availability`;

    config.manufacturer = "Xiaomi";
    config.model = "ESP8266 Deerma Humidifier";
    config.type = "custom";
    config.services = [
      {
        type: "thermostat",
        name: `${config.name} - Humidifier`,
        minTemperature: 0,
        maxTemperature: 30,
        temperatureDisplayUnitsValues: "CELSIUS",
        restrictHeatingCoolingState: [0, 3],
        topics: {
          getOnline: MQTT_TOPIC_AVAILABILITY,
          getTargetHeatingCoolingState: MQTT_TOPIC_READ,
          setTargetHeatingCoolingState: MQTT_TOPIC_WRITE,
          getCurrentRelativeHumidity: MQTT_TOPIC_READ,
          getTargetRelativeHumidity: MQTT_TOPIC_READ,
          setTargetRelativeHumidity: MQTT_TOPIC_WRITE,
          getCurrentTemperature: MQTT_TOPIC_READ,
          getTargetTemperature: MQTT_TOPIC_READ,
        },
      },
      {
        type: "switch",
        name: `${config.name} - Sound`,
        topics: {
          getOn: {
            topic: MQTT_TOPIC_READ,
            statePropertyName: "sound",
          },
          setOn: {
            topic: MQTT_TOPIC_WRITE,
            statePropertyName: "sound",
          },
        },
      },
      {
        type: "switch",
        name: `${config.name} - LED`,
        topics: {
          getOn: {
            topic: MQTT_TOPIC_READ,
            statePropertyName: "led",
          },
          setOn: {
            topic: MQTT_TOPIC_WRITE,
            statePropertyName: "led",
          },
        },
      },
      {
        type: "fan",
        name: `${config.name} - Fan`,
        topics: {
          getOn: {
            topic: MQTT_TOPIC_READ,
            statePropertyName: "state",
          },
          setOn: {
            topic: MQTT_TOPIC_WRITE,
            statePropertyName: "state",
          },
          getRotationSpeed: MQTT_TOPIC_READ,
          setRotationSpeed: MQTT_TOPIC_WRITE,
        },
      },
      //   {
      //     type: "humiditySensor",
      //     name: `${config.name} - Humidity sensor`,
      //     history: true,
      //     topics: {
      //       getCurrentRelativeHumidity: MQTT_TOPIC_READ,
      //     },
      //   },
      //   {
      //     type: "temperatureSensor",
      //     name: `${config.name} - Temperature sensor`,
      //     history: true,
      //     topics: {
      //       getCurrentTemperature: MQTT_TOPIC_READ,
      //     },
      //   },
    ];

    return {
      properties: {
        on: {
          encode(message, { extendedTopic }) {
            return JSON.stringify({
              [extendedTopic.statePropertyName]: message ? "on" : "off",
            });
          },
          decode(message, { extendedTopic }) {
            const state = JSON.parse(message);
            return (
              state.state === "on" &&
              state[extendedTopic.statePropertyName] === "on"
            );
          },
        },
        online: {
          decode(message) {
            return message === "online";
          },
        },
        rotationSpeed: {
          encode(message) {
            const valueRounded = Math.ceil(message / 25) * 25;
            if (valueRounded == 0) {
              return JSON.stringify({ state: "off" });
            }

            return JSON.stringify({
              mode: MAP_FAN_SPEED_TO_MODE[valueRounded],
            });
          },
          decode(message) {
            const state = JSON.parse(message);
            return MAP_FAN_MODE_TO_SPEED[state.mode];
          },
        },
        targetHeatingCoolingState: {
          encode(message) {
            return JSON.stringify({ state: message === "AUTO" ? "on" : "off" });
          },
          decode(message) {
            return JSON.parse(message).state === "on" ? "AUTO" : "OFF";
          },
        },
        targetRelativeHumidity: {
          encode(message) {
            return JSON.stringify({ humiditySetpoint: message });
          },
          decode(message) {
            return JSON.parse(message).humiditySetpoint;
          },
        },
        currentRelativeHumidity: {
          encode(message) {
            return JSON.stringify({ humiditySetpoint: message });
          },
          decode(message) {
            return JSON.parse(message).humidity;
          },
        },
        currentTemperature: {
          decode(message) {
            return JSON.parse(message).temperature;
          },
        },
        targetTemperature: {
          encode() {
            console.log("Setting target temperature is not supported");
            return undefined;
          },
          decode(message) {
            return JSON.parse(message).temperature;
          },
        },
      },
    };
  },
};
