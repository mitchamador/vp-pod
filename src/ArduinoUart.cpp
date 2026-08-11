#ifdef ARDUINO
#include "ArduinoUart.h"

ArduinoUart::ArduinoUart(Stream &stream)
    : _stream(stream)
{
}

int ArduinoUart::available()
{
    return _stream.available();
}

int ArduinoUart::read()
{
    return _stream.read();
}

size_t ArduinoUart::write(const uint8_t *data, size_t length)
{
    return _stream.write(data, length);
}
#endif