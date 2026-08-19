#pragma once

#ifdef ARDUINO
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#include "esPod_conf.h" // ZERO_VOLUME_FIX
#include "IBluetoothPlaybackSource.h"
#include "IBluetoothSourceEvents.h"
#include "IAudioOutput.h"

/// @brief Bluetooth backend built on top of the ESP32-A2DP library
/// (BluetoothA2DPSink). Owns everything that is specific to that library:
/// AVRCP metadata parsing/queueing, connection/audio-state callbacks, iOS
/// zero-volume-after-connect workaround (ZERO_VOLUME_FIX).
///
/// esPod (and main.cpp) only see this class through IBluetoothPlaybackSource
/// / IBluetoothSourceEvents - none of the A2DP/AVRCP specifics leak out.
class Esp32A2dpBluetoothSource : public IBluetoothPlaybackSource
{
public:
    /// @param a2dpSink The (already constructed, but not yet begin()'d)
    /// BluetoothA2DPSink instance. Its output stream (I2S/codec) is board-
    /// specific wiring and stays owned by main.cpp, same as before.
    /// @param audioOutput Only needed on boards where the A2DP sink's own
    /// output isn't used directly and audio is instead written manually
    /// from the raw stream-reader callback (non-AUDIOKIT boards, see
    /// original read_data_stream()). Pass nullptr if not applicable.
    /// NOTE: unlike NativeA2dpBluetoothSource, this only calls
    /// audioOutput->begin() once at startup with a fixed 44100/16/2 -
    /// BluetoothA2DPSink doesn't expose a codec-change callback on this
    /// code path (AUDIOKIT boards reconfigure their I2SCodecStream
    /// internally instead and don't go through this parameter at all).
    explicit Esp32A2dpBluetoothSource(BluetoothA2DPSink &a2dpSink, IAudioOutput *audioOutput = nullptr);

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
    BluetoothA2DPSink &_a2dp;
    IAudioOutput *_audioOutput = nullptr;
    IBluetoothSourceEvents *_sink = nullptr;

    // Only one instance is expected to exist (ESP32-A2DP callbacks are
    // plain C function pointers, they cannot carry a `this`), so we keep a
    // static back-pointer for the trampolines below.
    static Esp32A2dpBluetoothSource *_instance;

    volatile esp_a2d_connection_state_t _connectionState{};

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

    // AVRC metadata is delivered attribute-by-attribute via a C callback
    // from a Bluetooth stack task; we queue it and coalesce it into a single
    // TrackMetadata update on a dedicated FreeRTOS task, same as before.
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

    // ESP32-A2DP callback trampolines (C function pointers -> _instance)
    static void _connectionStateChangedTrampoline(esp_a2d_connection_state_t state, void *ptr);
    static void _audioStateChangedTrampoline(esp_a2d_audio_state_t state, void *ptr);
    static void _avrcConnectionStateTrampoline(bool connected);
    static void _avrcMetadataTrampoline(uint8_t id, const uint8_t *text);
    static void _avrcPlayPosTrampoline(uint32_t playPos);
#ifdef ZERO_VOLUME_FIX
    static void _avrcVolumeChangeTrampoline(int volume);
    static void _avrcVolumeChangeCompletedTrampoline(int volume);
#endif
    static void _readDataStreamTrampoline(const uint8_t *data, uint32_t length);
    static void _avrcTrackChangeTrampoline(uint8_t *uid);
};

#endif