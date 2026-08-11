#ifndef ARDUINO

#include "NativeI2sAudioOutput.h"

#include "esp_log.h"

NativeI2sAudioOutput::NativeI2sAudioOutput(i2s_port_t port, int pinBck, int pinWs, int pinDataOut)
    : _port(port), _pinBck(pinBck), _pinWs(pinWs), _pinDataOut(pinDataOut)
{
}

NativeI2sAudioOutput::~NativeI2sAudioOutput()
{
    if (_txChan != nullptr)
    {
        i2s_channel_disable(_txChan);
        i2s_del_channel(_txChan);
    }
}

void NativeI2sAudioOutput::begin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels)
{
    if (_txChan != nullptr)
    {
        // Sample rate/bit depth genuinely can change (different codec,
        // different peer). Tearing down and recreating the channel is the
        // safest way to reconfigure, rather than relying on in-place
        // reconfig calls that may not cover every field we care about.
        i2s_channel_disable(_txChan);
        i2s_del_channel(_txChan);
        _txChan = nullptr;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(_port, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &_txChan, nullptr);
    if (err != ESP_OK)
    {
        ESP_LOGE("I2S_OUT", "i2s_new_channel failed: %s", esp_err_to_name(err));
        _txChan = nullptr;
        return;
    }

    i2s_slot_mode_t mono_or_stereo = (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            static_cast<i2s_data_bit_width_t>(bitsPerSample),
            mono_or_stereo),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(_pinBck),
            .ws = static_cast<gpio_num_t>(_pinWs),
            .dout = static_cast<gpio_num_t>(_pinDataOut),
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(_txChan, &std_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE("I2S_OUT", "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
        i2s_del_channel(_txChan);
        _txChan = nullptr;
        return;
    }

    err = i2s_channel_enable(_txChan);
    if (err != ESP_OK)
    {
        ESP_LOGE("I2S_OUT", "i2s_channel_enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(_txChan);
        _txChan = nullptr;
        return;
    }

    ESP_LOGI("I2S_OUT", "I2S configured: %uHz, %ubit, %uch", sampleRate, bitsPerSample, channels);
}

size_t NativeI2sAudioOutput::write(const uint8_t *data, size_t length)
{
    if (_txChan == nullptr)
        return 0;

    size_t written = 0;
    i2s_channel_write(_txChan, data, length, &written, portMAX_DELAY);
    return written;
}

void NativeI2sAudioOutput::stop()
{
    // Disabling the channel discards whatever is still
    // queued in its DMA buffer - there's no separate "zero the buffer"
    // call in the new driver like the legacy one had.
    if (_txChan != nullptr)
    {
        i2s_channel_disable(_txChan);
    }

}

#endif // ARDUINO
