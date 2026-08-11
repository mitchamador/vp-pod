#pragma once

#ifdef ARDUINO

#include "AudioTools.h"
#include "IAudioOutput.h"

/// @brief IAudioOutput backed by AudioTools' I2SStream (Arduino framework,
/// non-AUDIOKIT boards - AUDIOKIT boards wire I2SCodecStream directly into
/// BluetoothA2DPSink itself and don't go through this class at all).
class Esp32I2sAudioOutput : public IAudioOutput
{
public:
    Esp32I2sAudioOutput(I2SStream &i2s, int pinBck, int pinWs, int pinData);

    void begin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels) override;
    size_t write(const uint8_t *data, size_t length) override;
    void stop() override;

private:
    I2SStream &_i2s;
    int _pinBck, _pinWs, _pinData;
};

#endif