#pragma once

#include "driver/uart.h"
#include "IUart.h"

class EspIdfUart : public IUart
{
public:
    /// @brief Configures pins, installs the driver and starts it - all
    /// synchronously in the constructor. This matters: esPod is typically
    /// constructed as a global object that spawns an RX task in its own
    /// constructor, and C++ guarantees objects in the same translation unit
    /// are constructed in declaration order - so as long as this object is
    /// declared before esPod, the driver is guaranteed ready before that
    /// task's first read.
    EspIdfUart(uart_port_t port, int rxPin, int txPin, uint32_t baudRate = 19200);

    int available() override;
    int read() override;
    size_t write(const uint8_t *data, size_t len) override;

private:
    uart_port_t _port;
};