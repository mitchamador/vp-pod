#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Abstraction over "where decoded PCM audio actually goes" (I2S/
/// codec output). Both Bluetooth backends (Esp32A2dpBluetoothSource,
/// NativeA2dpBluetoothSource) talk to this instead of a concrete I2S
/// object, so neither one needs to know about AudioTools, the ESP-IDF I2S
/// driver, or any particular board's pin wiring.
class IAudioOutput
{
public:
    virtual ~IAudioOutput() = default;

    /// @brief (Re)configures the output for a given PCM format. Called once
    /// at connect time, and again whenever the Bluetooth backend's codec
    /// negotiation reports a different sample rate / bit depth / channel
    /// count than before (e.g. switching between SBC and AAC/aptX, or a
    /// different peer with different capabilities) - so callers must not
    /// assume a fixed 44100/16/2 format.
    virtual void begin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels) = 0;

    /// @brief Writes raw PCM data. Returns the number of bytes actually
    /// written (mirrors typical I2S write semantics - may be less than
    /// length under backpressure, callers should not treat that as an
    /// error).
    virtual size_t write(const uint8_t *data, size_t length) = 0;

    /// @brief Stops audible output immediately - specifically to clear
    /// whatever is still sitting in the DMA buffer on disconnect, so it
    /// doesn't keep looping stale audio with no new data coming in. Does
    /// NOT tear down the underlying driver/config; write() should keep
    /// working immediately afterwards once real audio resumes (e.g. on
    /// reconnect) without needing another begin() call.
    virtual void stop() = 0;
};
