#pragma once

#include "driver/uart.h"
#include "IUart.h"

class EspIdfUart : public IUart
{
public:
    explicit EspIdfUart(uart_port_t port);

    int available() override;
    int read() override;
    size_t write(const uint8_t *data, size_t len) override;

private:
    uart_port_t _port;
};