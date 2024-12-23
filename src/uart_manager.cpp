#include "uart_manager.h"
#include <cstring> // for memset, etc.

AsyncUartManager::AsyncUartManager(Stream& serial, size_t bufferSize)
    : _serial(serial),
      _rxBufferSize(bufferSize)
{
    _rxBuffer = new char[_rxBufferSize];
    memset(_rxBuffer, 0, _rxBufferSize);
}

void AsyncUartManager::begin(unsigned long baudRate) {
    // If we want to do hardware config:
    // static_cast<HardwareSerial&>(_serial).begin(baudRate);
    // or do it externally in main setup().
}

void AsyncUartManager::poll() {
    // Read all available bytes in small chunks, non-blocking.
    while (_serial.available() > 0) {
        char c = _serial.read();
        if (c == '\r' || c == '\n') {
            // We got a full line => callback
            if (_handler && _rxIndex > 0) {
                _rxBuffer[_rxIndex] = '\0'; // null-terminate
                _handler->onLineReceived(_rxBuffer);
            }
            // Reset for next line
            memset(_rxBuffer, 0, _rxBufferSize);
            _rxIndex = 0;
        } else {
            // Add to buffer
            if (_rxIndex < _rxBufferSize - 1) {
                _rxBuffer[_rxIndex++] = c;
            } else {
                // Buffer overflow => reset or handle error
                _rxIndex = 0;
                memset(_rxBuffer, 0, _rxBufferSize);
            }
        }
    }
}

void AsyncUartManager::setHandler(IAsyncUartHandler* handler) {
    _handler = handler;
}

void AsyncUartManager::writeLine(const char* line) {
    _serial.print(line);
    _serial.print("\r\n");
}
