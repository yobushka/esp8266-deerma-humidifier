#include "web_server_manager.h"
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

WebServerManager::WebServerManager(ConfigManager &c, MqttManager &m)
: config(c), mqtt(m), server(80) {}

void WebServerManager::begin(bool sm) {
    settingMode = sm;

    // Serve main PWA page
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleRoot(request);
    });

    // JSON API endpoints
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleApiStatus(request);
    });

    server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleApiSettings(request);
    });

    server.on("/api/setap", HTTP_POST, [this](AsyncWebServerRequest *request){},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0 && total == len) {
                handleApiSetAp(request, data, len);
            }
        }
    );

    server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleApiReset(request);
    });

    // Manifest and Service Worker
    server.on("/manifest.json", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getManifest());
    });
    server.on("/service-worker.js", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "application/javascript", getServiceWorker());
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    });

    server.onNotFound([this](AsyncWebServerRequest *request) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    server.begin();
}

void WebServerManager::handleRoot(AsyncWebServerRequest *request) {
    String page = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                  "<meta name=\"theme-color\" content=\"#3079ed\" />"
                  "<title>ESP Device</title>"
                  "<link rel=\"manifest\" href=\"/manifest.json\">"
                  "<script>"
                  "if ('serviceWorker' in navigator) {"
                  "  window.addEventListener('load', function() {"
                  "    navigator.serviceWorker.register('/service-worker.js');"
                  "  });"
                  "}"
                  "</script>"
                  "<style>" + getStyles() + "</style>"
                  "</head><body><div class=\"container\">"
                  "<header class=\"app-header\"><h1>ESP Device PWA (Async)</h1></header>"
                  "<main>"
                  "<div id=\"status\"></div>"
                  "<div id=\"actions\">"
                  "<button id=\"reloadBtn\">Reload Status</button>"
                  "<button id=\"resetBtn\">Reset Config</button>"
                  "</div>"
                  "</main>"
                  "<footer><p>Powered by ESP8266</p></footer>"
                  "<script>"
                  "async function fetchStatus() {"
                  "  const res = await fetch('/api/status');"
                  "  if(!res.ok) { document.getElementById('status').innerText='Error loading status'; return; }"
                  "  const data = await res.json();"
                  "  document.getElementById('status').innerHTML = `<p><strong>Hostname:</strong> ${data.hostname}</p>`"
                  "    + `<p><strong>Location:</strong> ${data.location}</p>`"
                  "    + `<p><strong>WiFi IP:</strong> ${data.wifi_ip}</p>`"
                  "    + `<p><strong>RSSI:</strong> ${data.rssi}</p>`;"
                  "}"
                  "document.getElementById('reloadBtn').addEventListener('click', fetchStatus);"
                  "document.getElementById('resetBtn').addEventListener('click', async ()=>{"
                  "  const res = await fetch('/api/reset',{method:'POST'});"
                  "  if(res.ok){"
                  "    document.getElementById('status').innerText='Device resetting...';"
                  "  } else {"
                  "    document.getElementById('status').innerText='Reset failed';"
                  "  }"
                  "});"
                  "fetchStatus();"
                  "</script>"
                  "</div></body></html>";
    request->send(200, "text/html", page);
}

void WebServerManager::handleApiStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    doc["hostname"] = config.HOSTNAME;
    doc["location"] = config.LOCATION;
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void WebServerManager::handleApiSettings(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    doc["ssid"] = config.ssid;
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["debugEnabled"] = config.debugEnabled;
    doc["settingMode"] = settingMode;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void WebServerManager::handleApiSetAp(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, data, len);
    if (!error) {
        strlcpy(config.HOSTNAME, doc["hostname"] | "", sizeof(config.HOSTNAME));
        strlcpy(config.LOCATION, doc["location"] | "", sizeof(config.LOCATION));
        strlcpy(config.ssid, doc["ssid"] | "", sizeof(config.ssid));
        strlcpy(config.pass, doc["pass"] | "", sizeof(config.pass));
        strlcpy(config.mqtt_server, doc["mqtt_server"] | "", sizeof(config.mqtt_server));
        strlcpy(config.mqtt_port, doc["mqtt_port"] | "1883", sizeof(config.mqtt_port));
        strlcpy(config.mqtt_user, doc["mqtt_user"] | "", sizeof(config.mqtt_user));
        strlcpy(config.mqtt_password, doc["mqtt_password"] | "", sizeof(config.mqtt_password));
        config.debugEnabled = doc["debug"] | false;

        bool saved = config.saveConfig();
        DynamicJsonDocument resp(128);
        resp["success"] = saved;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
        if (saved) {
            delay(2000);
            ESP.restart();
        }
        return;
    }
    request->send(400, "application/json", "{\"error\":\"Bad Request\"}");
}

void WebServerManager::handleApiReset(AsyncWebServerRequest *request) {
    SPIFFS.begin();
    SPIFFS.remove("/config.json");
    DynamicJsonDocument doc(64);
    doc["success"] = true;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    delay(2000);
    ESP.restart();
}

String WebServerManager::IpAddress2String(const IPAddress& ipAddress) {
    return String(ipAddress[0]) + "." + String(ipAddress[1]) + "." + String(ipAddress[2]) + "." + String(ipAddress[3]);
}

String WebServerManager::getStyles() {
    return R"CSS(
body {
  font-family: Arial, sans-serif;
  background: #f9f9f9;
  margin: 0;
  padding: 0;
  color: #333;
}
.container {
  max-width: 600px;
  margin: 20px auto;
  background: #fff;
  border-radius: 8px;
  padding: 20px;
  box-shadow: 0 2px 4px rgba(0,0,0,0.1);
}
.app-header h1 {
  margin: 0;
}
button {
  padding: 10px;
  margin: 5px;
  background: #3079ed;
  color: #fff;
  border: none;
  border-radius: 4px;
}
button:hover {
  background: #245dc1;
}
)CSS";
}

String WebServerManager::getManifest() {
    return R"JSON(
{
  "name": "ESP Device",
  "short_name": "ESPDev",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#ffffff",
  "theme_color": "#3079ed",
  "icons": []
}
)JSON";
}

String WebServerManager::getServiceWorker() {
    return R"JS(
const CACHE_NAME = 'esp-device-cache-v1';
const urlsToCache = [
  '/',
  '/manifest.json',
  '/service-worker.js'
];

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(CACHE_NAME).then(cache => {
      return cache.addAll(urlsToCache);
    })
  );
});

self.addEventListener('fetch', event => {
  event.respondWith(
    caches.match(event.request).then(response => {
      return response || fetch(event.request);
    })
  );
});
)JS";
}
