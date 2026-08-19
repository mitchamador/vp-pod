#pragma once

#include <stddef.h>
#include <stdint.h>
#include "platform.h"

enum PLAY_STATUS : uint8_t
{
    PLAY_STATUS_PLAY = 0x00,
    PLAY_STATUS_STOP = 0x01,
    PLAY_STATUS_PAUSE = 0x02
};


/// @brief Abstraction over "where decoded PCM audio actually goes" (I2S/
/// codec output). Both Bluetooth backends (Esp32A2dpBluetoothSource,
/// NativeA2dpBluetoothSource) talk to this instead of a concrete I2S
/// object, so neither one needs to know about AudioTools, the ESP-IDF I2S
/// driver, or any particular board's pin wiring.
class IAudioOutput
{
private:
    /// @brief (Re)configures the output for a given PCM format. Called once
    /// at connect time, and again whenever the Bluetooth backend's codec
    /// negotiation reports a different sample rate / bit depth / channel
    /// count than before (e.g. switching between SBC and AAC/aptX, or a
    /// different peer with different capabilities) - so callers must not
    /// assume a fixed 44100/16/2 format.
    virtual void doBegin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels) = 0;

    /// @brief Writes raw PCM data. Returns the number of bytes actually
    /// written (mirrors typical I2S write semantics - may be less than
    /// length under backpressure, callers should not treat that as an
    /// error).
    virtual size_t doWrite(const uint8_t* data, size_t length) = 0;

    /// @brief Stops audible output immediately - specifically to clear
    /// whatever is still sitting in the DMA buffer on disconnect, so it
    /// doesn't keep looping stale audio with no new data coming in. Does
    /// NOT tear down the underlying driver/config; write() should keep
    /// working immediately afterwards once real audio resumes (e.g. on
    /// reconnect) without needing another begin() call.
    virtual void doStop() = 0;

protected:
    static constexpr uint32_t REAL_POSITION_STALE_MS = 3000;
    bool _isPlaying = false;
    uint64_t _rawAudioDataBytesReceived = 0;
    uint64_t _prevRawAudioDataBytesReceived = 0;
    uint32_t _lastRealPositionTimestamp = UINT32_MAX;
    uint32_t _estimatedPositionMs = 0;
    uint32_t _bytesPerSecond = 0;
    
    // Only count bytes if audio is actively flowing. On phones where the
    // real AVRC position notification never fires (iOS), this is the only
    // source of play-position updates - throttled to roughly 2x/second.
    // When the real callback IS active and recent, back off and let it win.
    void updateEstimatedPosition(size_t length) {
        if (!_isPlaying || _bytesPerSecond <= 0.0) return;

        _rawAudioDataBytesReceived += length;
        
        if (_rawAudioDataBytesReceived > _prevRawAudioDataBytesReceived + (_bytesPerSecond >> 1)) {
            _prevRawAudioDataBytesReceived = _rawAudioDataBytesReceived;

            bool realPositionRecent = _lastRealPositionTimestamp != UINT32_MAX &&
                                      (platform::time_now_ms() - _lastRealPositionTimestamp) < REAL_POSITION_STALE_MS;

            if (!realPositionRecent) {
                uint32_t estimatedPositionMs = (uint32_t)(_rawAudioDataBytesReceived / (_bytesPerSecond / 1000.0));
                if (estimatedPositionMs > 500) {
                    _estimatedPositionMs = estimatedPositionMs;
                }
            } else {
                _estimatedPositionMs = 0;
            }
        }
    }

public:
    virtual ~IAudioOutput() = default;

    void begin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels) {
        _bytesPerSecond = sampleRate * (bitsPerSample / 8.0) * channels;
        _rawAudioDataBytesReceived = 0;
        _prevRawAudioDataBytesReceived = 0;
        _lastRealPositionTimestamp = UINT32_MAX;
        _estimatedPositionMs = 0;
        doBegin(sampleRate, bitsPerSample, channels);
    }

    size_t write(const uint8_t* data, size_t length) {
        size_t written = doWrite(data, length);
        updateEstimatedPosition(written);
        return written;
    }

    void stop() {
        _isPlaying = false;
        _rawAudioDataBytesReceived = 0;
        _prevRawAudioDataBytesReceived = 0;
        _lastRealPositionTimestamp = UINT32_MAX;
        doStop();
    }

    uint32_t _getEstimatedPosition()
    {
        return _estimatedPositionMs;
    }

    void _resetPlayPositionEstimate()
    {
        _estimatedPositionMs = 0;
        _rawAudioDataBytesReceived = 0;
        _prevRawAudioDataBytesReceived = 0;
    }

    void _updatePlayingState(PLAY_STATUS status)
    {
        if (status == PLAY_STATUS_PLAY) {
            _isPlaying = true;
        } else if (status == PLAY_STATUS_STOP) {
            _isPlaying = false;
            _resetPlayPositionEstimate();
        } else {
            _isPlaying = false;
        }
    }

    // A real notification arrived - remember when, so the byte-based
    // estimate (fed from read_data_stream, which is what actually
    // fires on iOS since this callback never does) backs off and lets the
    // real value win instead of immediately overwriting it.
    void _updateLastRealPositionTimestamp()
    {
        _lastRealPositionTimestamp = platform::time_now_ms();
    }

};
