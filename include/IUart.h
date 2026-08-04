#pragma once

#include <stddef.h>
#include <stdint.h>

class IUart
{
public:
    virtual ~IUart() {}

    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(const uint8_t *data, size_t length) = 0;
};