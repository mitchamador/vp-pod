#pragma once

#include <Stream.h>
#include "IUart.h"

class ArduinoUart : public IUart
{
public:
    explicit ArduinoUart(Stream &stream);

    int available() override;
    int read() override;
    size_t write(const uint8_t *data, size_t len) override;

private:
    Stream &_stream;
};