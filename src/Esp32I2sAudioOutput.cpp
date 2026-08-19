#ifdef ARDUINO

#include "Esp32I2sAudioOutput.h"

Esp32I2sAudioOutput::Esp32I2sAudioOutput(I2SStream &i2s, int pinBck, int pinWs, int pinData)
    : _i2s(i2s), _pinBck(pinBck), _pinWs(pinWs), _pinData(pinData)
{
}

void Esp32I2sAudioOutput::doBegin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels)
{
    auto cfg = _i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = _pinBck;
    cfg.pin_ws = _pinWs;
    cfg.pin_data = _pinData;
    cfg.sample_rate = sampleRate;
    cfg.bits_per_sample = bitsPerSample;
    cfg.channels = channels;
    cfg.i2s_format = I2S_LSB_FORMAT;
    _i2s.begin(cfg);
}

size_t Esp32I2sAudioOutput::doWrite(const uint8_t *data, size_t length)
{
    return _i2s.write(data, length);
}

void Esp32I2sAudioOutput::doStop()
{
    // do nothing
}

#endif
