#include "EspIdfUart.h"

EspIdfUart::EspIdfUart(uart_port_t port, int rxPin, int txPin, uint32_t baudRate)
    : _port(port)
{
    uart_config_t cfg = {
        .baud_rate = (int)baudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(_port, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(_port, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(_port, 1024, 1024, 0, nullptr, 0));
    uart_flush(_port);
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
