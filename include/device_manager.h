#pragma once
#include "uart_manager.h"

// Example device state
enum class HumMode : int8_t {
    Unknown = -1,
    Low     = 1,
    Medium  = 2,
    High    = 3,
    Setpoint= 4
};

struct DeviceState {
    bool    powerOn           = false;
    HumMode mode              = HumMode::Unknown;
    int     humiditySetpoint  = 40;
    int     currentHumidity   = 0;
    // etc...
};

// DeviceManager implements IAsyncUartHandler so it receives lines from the device
class DeviceManager : public IAsyncUartHandler {
public:
    explicit DeviceManager(AsyncUartManager& uart);

    void begin();
    
    // Public commands to set device state
    void setPower(bool on);
    void setMode(HumMode mode);
    // etc...

    // Implementation of IAsyncUartHandler:
    void onLineReceived(const char* line) override;

    // Access the device state
    const DeviceState& state() const { return _state; }

private:
    AsyncUartManager& _uart;
    DeviceState _state;

    // For sending commands like "set_properties 2 1 true"
    void queueSetProperty(int siid, int piid, const char* value);
};
