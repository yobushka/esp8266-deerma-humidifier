#include "device_manager.h"
#include <Arduino.h>
#include <cstring> // for strncmp, etc.

DeviceManager::DeviceManager(AsyncUartManager& uart)
    : _uart(uart)
{
    // Let the UartManager know we are the callback handler
    _uart.setHandler(this);
}

void DeviceManager::begin() {
    // Possibly send initialization or check the device
}

// Example public commands
void DeviceManager::setPower(bool on) {
    queueSetProperty(2, 1, on ? "true" : "false");
}

void DeviceManager::setMode(HumMode mode) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(mode));
    queueSetProperty(2, 5, buf);
}

// The onLineReceived callback is “async” – it’s called whenever
// the UART manager sees a full line (non-blocking).
void DeviceManager::onLineReceived(const char* line) {
    // parse line
    if (strncmp(line, "properties_changed", 18) == 0) {
        // parse "properties_changed <siid> <piid> <value>"
        int siid = 0;
        int piid = 0;
        char value[16] = {0};

        int scanned = sscanf(line, "properties_changed %d %d %15s", &siid, &piid, value);
        if (scanned == 3) {
            // update _state
            if (siid == 2 && piid == 1) {
                _state.powerOn = (strncmp(value, "true", 4) == 0);
            }
            // etc...
        }
        // The device might expect "ok\r\n"? Let's do it:
        _uart.writeLine("ok");
        return;
    }

    if (strncmp(line, "get_down", 8) == 0) {
        // Device wants next command. If you have a queue, do it here.
        // Or just respond with “none”
        _uart.writeLine("down none"); 
        return;
    }

    // else handle "net", "mcu_version", etc.
}

void DeviceManager::queueSetProperty(int siid, int piid, const char* value) {
    // Example "set_properties 2 1 true"
    char msg[64];
    snprintf(msg, sizeof(msg), "set_properties %d %d %s", siid, piid, value);
    _uart.writeLine(msg);  // sends instantly with \r\n
}
