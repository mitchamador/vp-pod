#pragma once

#ifndef ARDUINO

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "IAudioOutput.h"

/// @brief IAudioOutput backed directly by the ESP-IDF I2S driver - no
/// AudioTools/Arduino dependency, for the native (pure ESP-IDF) build.
/// Re-installs the I2S driver on every begin() call, since sample
/// rate/bit depth can genuinely change between codecs/peers and the I2S
/// driver doesn't support reconfiguring an already-installed instance.
///
/// Uses the legacy driver/i2s.h API deliberately - do NOT include this
/// header (or link this .cpp) into an Arduino-framework build: recent
/// arduino-esp32 core versions use the new i2s_std driver internally, and
/// having both the legacy and new I2S drivers active causes a boot loop
/// ("CONFLICT! The new i2s driver can't work along with the legacy i2s
/// driver"). Hence the #ifndef ARDUINO around this entire file.
class NativeI2sAudioOutput : public IAudioOutput
{
public:
    NativeI2sAudioOutput(i2s_port_t port, int pinBck, int pinWs, int pinDataOut);
    ~NativeI2sAudioOutput() override;

    void begin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels) override;
    size_t write(const uint8_t *data, size_t length) override;
    void stop() override;

private:
    i2s_port_t _port;
    int _pinBck, _pinWs, _pinDataOut;
    bool _installed = false;
    i2s_chan_handle_t _txChan;
};

#endif // ARDUINO
