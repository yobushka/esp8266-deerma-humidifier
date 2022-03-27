# esp8266-deerma-humidifier-v2

This is a custom firmware for the ESP8285-based Wi-Fi module of the Xiaomi Mi Smart Antibacterial Humidifier,
which replaces cloud connectivity with local mqtt controls.

The internal Mi Model ID of the supported device is `deerma.humidifier.jsq`.
The Model on the packaging being `ZNJSQ01DEM`.

Communication is done via a few MQTT Topics:

- `esp8266-deerma-humidifier/HUMIDIFIER-%CHIP_ID%/status`
- `esp8266-deerma-humidifier/HUMIDIFIER-%CHIP_ID%/state`
- `esp8266-deerma-humidifier/HUMIDIFIER-%CHIP_ID%/command`

`/status` will either contain `online` or `offline` and is also used for the LWT.

`/state` will contain a JSON state which looks like this:

```json
{
  "state": "on",
  "mode": "setpoint",
  "humiditySetpoint": 45,
  "humidity": 41,
  "temperature": 24,
  "sound": "off",
  "led": "off",
  "waterTank": "full",
  "wifi": {
    "ssid": "Das IoT",
    "ip": "10.0.13.37",
    "rssi": -51
  }
}
```

and of course you can control the device via the `/control` topic which expects the same JSON structure as the state provides.<br/>
That also means that you can run multiple commands at once. For example if you wanted to turn LED off but Sound on, you'd publish

```json
{
  "sound": "on",
  "led": "off"
}
```

to the command topic. Keep in mind, that obviously not all state properties are writable.

If you ever want to update the firmware, you can do so without disassembly thanks to ArduinoOTA.

The default password is the hostname, which is mostly there to prevent accidental firmware updates.
