#pragma once

#include <stdint.h>
#include "esPod_conf.h"
#include "SettingsKeys.h"
#include "esp_log.h"

#define DEFAULT_VOLUME 64
#define INVALID_VOLUME -1

class IBluetoothSourceEvents;

/// @brief Implemented by a Bluetooth backend (e.g. Esp32A2dpBluetoothSource,
/// or later a UART-connected BT module). esPod calls into this interface to
/// control playback on the remote (phone) side - it never talks to a
/// specific Bluetooth stack (A2DP/AVRCP, vendor AT commands, ...) directly.
class IBluetoothPlaybackSource
{
protected:

    // AVRC metadata is delivered attribute-by-attribute via a C callback
    // from a Bluetooth stack task; we queue it and coalesce it into a single
    // TrackMetadata update on a dedicated FreeRTOS task, same as before.
    struct AvrcMetadataItem
    {
        uint8_t id = 0;
        uint8_t *payload = nullptr;
    };
    
    bool _zeroVolumeFixEnabled = ZERO_VOLUME_FIX_DEFAULT;
    
    enum class VolumeState {
        AfterConnectionNotDefined,
        AfterConnectionSet
    } _volumeState = VolumeState::AfterConnectionNotDefined;

    void _onConnected() { _volumeState = VolumeState::AfterConnectionNotDefined; }

    void _restoreVolume(int reportedVolume) {
        if (!_zeroVolumeFixEnabled || _volumeState != VolumeState::AfterConnectionNotDefined) return;

        ESP_LOGD("BT_SRC", "_restoreVolume to %d", reportedVolume);

        _volumeState = VolumeState::AfterConnectionSet;
        if (reportedVolume == 0 || reportedVolume == INVALID_VOLUME) {
            if (reportedVolume == 0) {
                ESP_LOGI("BT_SRC", "Volume is set to 0 after connecting. Restore volume to saved value");
            } else {
                ESP_LOGI("BT_SRC", "Volume is not set after connecting. Restore volume to saved value");
            }
            int saved = storage::getInt(SettingsKeys::Volume, DEFAULT_VOLUME);
            if (saved != 0) doSetVolume(saved);
        }
    }

    void _saveVolume() {
        if (!_zeroVolumeFixEnabled) return;

        ESP_LOGD("BT_SRC", "_saveVolume");

        int current = doGetVolume();
        if (current != storage::getInt(SettingsKeys::Volume, DEFAULT_VOLUME))
            storage::setInt(SettingsKeys::Volume, current);
    }

public:
    virtual ~IBluetoothPlaybackSource() = default;

    void loadSettingsFromStorage() {
        _zeroVolumeFixEnabled = storage::getBool(SettingsKeys::ZeroVolumeFix, ZERO_VOLUME_FIX_DEFAULT);
    }

    /// @brief Starts the backend (advertising/connectable, etc).
    /// @param deviceName Name to advertise to peers.
    virtual void begin(const char *deviceName) = 0;

    /// @brief Registers the sink that will receive Bluetooth-side events
    /// (connection state, metadata, play position, ...). Must be called
    /// before begin().
    virtual void setEventSink(IBluetoothSourceEvents &sink) = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void fast_forward() = 0;
    virtual void rewind() = 0;
    virtual void volume_up() = 0;
    virtual void volume_down() = 0;

    virtual bool isConnected() const = 0;

    /// @brief Disconnects the current peer (if any) and forgets it, so the
    /// next connection attempt starts fresh rather than auto-reconnecting.
    /// Used e.g. by a physical "reset pairing" button.
    virtual void forgetConnection() = 0;

    virtual void doSetVolume(uint8_t volume) = 0;
    virtual uint8_t doGetVolume() = 0;
};
