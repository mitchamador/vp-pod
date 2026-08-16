#pragma once

#include "NativeA2dpBluetoothSink.h"

#include "esPod_conf.h" // TRACK_POSITION_FIX / ZERO_VOLUME_FIX
#include "IBluetoothPlaybackSource.h"
#include "IBluetoothSourceEvents.h"
#include "IAudioOutput.h"

/// @brief Bluetooth backend built on top of NativeA2DPSink (direct ESP-IDF
/// Bluedroid A2DP/AVRC usage, patched for extra codecs - see
/// NativeA2dpBluetoothSink.h). Same role as Esp32A2dpBluetoothSource, just wired to
/// a different underlying A2DP/AVRCP implementation - esPod and main.cpp
/// only ever see IBluetoothPlaybackSource/IBluetoothSourceEvents, not this.
class NativeA2dpBluetoothSource : public IBluetoothPlaybackSource
{
public:
    /// @param audioOutput Optional: where decoded PCM is written as it
    /// arrives. Also gets begin() called whenever the negotiated codec
    /// format changes (sample rate/bit depth/channels - AAC/aptX won't
    /// necessarily be 44100/16/2). Pass nullptr if not applicable.
    explicit NativeA2dpBluetoothSource(NativeA2DPSink &a2dpSink, IAudioOutput *audioOutput = nullptr);

    void begin(const char *deviceName) override;
    void setEventSink(IBluetoothSourceEvents &sink) override { _sink = &sink; }

    void play() override { _a2dp.play(); }
    void pause() override { _a2dp.pause(); }
    void stop() override { _a2dp.stop(); }
    void next() override;
    void previous() override;
    void fast_forward() override { _a2dp.fast_forward(); }
    void rewind() override { _a2dp.rewind(); }
    void volume_up() override { _a2dp.volume_up(); }
    void volume_down() override { _a2dp.volume_down(); }

    bool isConnected() const override;
    void forgetConnection() override;

private:
    NativeA2DPSink &_a2dp;
    IAudioOutput *_audioOutput = nullptr;
    IBluetoothSourceEvents *_sink = nullptr;

    // Same reasoning as Esp32A2dpBluetoothSource: NativeA2DPSink callbacks
    // are plain C function pointers, so a static back-pointer is needed.
    static NativeA2dpBluetoothSource *_instance;

#ifdef TRACK_POSITION_FIX
    static constexpr uint32_t BYTES_PER_SECOND = 176400;
    static constexpr uint32_t BYTES_POSITION_HUNK = BYTES_PER_SECOND / 2;
    static constexpr uint32_t REAL_POSITION_STALE_MS = 3000;

    volatile uint32_t _rawAudioDataBytesReceived = 0, _prevRawAudioDataBytesReceived = 0;
    volatile bool _isPlaying = false;
    volatile uint32_t _lastRealPositionTimestamp = UINT32_MAX;
    void _resetPlayPositionEstimate()
    {
        _rawAudioDataBytesReceived = 0;
        _prevRawAudioDataBytesReceived = 0;
    }
#endif

#ifdef ZERO_VOLUME_FIX
    enum class VolumeState
    {
        AfterConnectionNotDefined,
        AfterConnectionSet
    };
    VolumeState _volumeState = VolumeState::AfterConnectionNotDefined;
    static uint8_t _nvsGetVolume();
    static void _nvsSetVolume(uint8_t volume);
#endif

    // Same metadata coalescing approach as Esp32A2dpBluetoothSource: the
    // native sink's metadata callback fires once per attribute (title,
    // artist, ...) in a burst after a track-change notification; we queue
    // and debounce them into a single TrackMetadata update.
    struct AvrcMetadataItem
    {
        uint8_t id = 0;
        uint8_t *payload = nullptr;
    };
    QueueHandle_t _avrcMetadataQueue = nullptr;
    TaskHandle_t _processAvrcTaskHandle = nullptr;
    static void _processAvrcTask(void *pvParameters);
    void _runProcessAvrcTask();
    static void _filterPayload(char *output, const char *input);

    // NativeA2DPSink callback trampolines (C function pointers -> _instance)
    static void _connectionStateChangedTrampoline(esp_a2d_connection_state_t state, void *ptr);
    static void _audioStateChangedTrampoline(esp_a2d_audio_state_t state, void *ptr);
    static void _avrcConnectionStateTrampoline(bool connected);
    static void _avrcMetadataTrampoline(uint8_t id, const uint8_t *text);
    static void _avrcPlayPosTrampoline(uint32_t playPos);
#ifdef ZERO_VOLUME_FIX
    static void _avrcVolumeChangeTrampoline(int volume);
    static void _avrcVolumeChangeCompletedTrampoline(int volume);
#endif
    static void _streamReaderTrampoline(const uint8_t *data, uint32_t length);
    static void _codecConfigTrampoline(uint32_t rate, uint8_t bps, uint8_t channels);
    static void _avrcTrackChangeTrampoline(uint8_t *uid);
    static void _peerNameAvailableTrampoline(const char *name);
};
