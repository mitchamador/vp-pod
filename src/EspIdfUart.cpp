#include "EspIdfUart.h"

EspIdfUart::EspIdfUart(uart_port_t port)
    : _port(port)
{
}

int EspIdfUart::available()
{
    size_t len = 0;
    uart_get_buffered_data_len(_port, &len);
    return static_cast<int>(len);
}

int EspIdfUart::read()
{
    uint8_t b;

    if (uart_read_bytes(_port, &b, 1, 0) == 1)
        return b;

    return -1;
}

size_t EspIdfUart::write(const uint8_t *data, size_t length)
{
    return uart_write_bytes(
        _port,
        reinterpret_cast<const char *>(data),
        length);
}
