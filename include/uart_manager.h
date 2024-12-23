#pragma once
#include <Arduino.h>

// Forward-declare a handler interface for “line received”
class IAsyncUartHandler {
public:
    virtual ~IAsyncUartHandler() {}
    // Called whenever we detect a full line
    virtual void onLineReceived(const char* line) = 0;
};

// A small “async-like” UART manager that never blocks for long
// and calls a handler callback whenever it sees a full line.
class AsyncUartManager {
public:
    // Constructor expects a reference to a Stream (Serial, Serial1, or SoftwareSerial)
    explicit AsyncUartManager(Stream& serial, size_t bufferSize = 256);

    // If you want to also do Serial.begin(...) here, you can.
    void begin(unsigned long baudRate = 115200);

    // Instead of a big blocking read, we do short polls
    // that read all available bytes, store them in a buffer,
    // and whenever we see '\r' or '\n', we call the handler callback.
    void poll();

    // Set a handler that gets line callbacks
    void setHandler(IAsyncUartHandler* handler);

    // Writes a line out, with \r\n appended if desired
    void writeLine(const char* line);

private:
    Stream& _serial;
    IAsyncUartHandler* _handler = nullptr;

    // Buffer stuff
    char*  _rxBuffer; 
    size_t _rxBufferSize;
    size_t _rxIndex = 0;
};
