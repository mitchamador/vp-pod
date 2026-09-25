#pragma once

#include <stdint.h>
#include <cstring>
#include <string>
#include <vector>
#include "esPod_conf.h"
#include "SettingsKeys.h"
#include "esp_log.h"
#include "esp_bt_defs.h"
#include "esp_gap_bt_api.h"
#include "nvs_flash.h"
#include "nvs.h"

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

    // Persistent "peer name" cache, keyed by MAC, for a future web UI.
    // Shared by every backend (native and Esp32A2dpBluetoothSource/Arduino)
    // rather than duplicated per backend, since the logic doesn't touch
    // any backend-specific state - it's pure NVS + classic-BT GAP calls.
    // The classic-BT bond table (esp_bt_gap_get_bond_device_num/_list) is
    // the single source of truth; this is only ever a cache on top of it.
    static void _macToKey(const esp_bd_addr_t bda, char out[16]) {
        // "pn_" + 12 hex chars = 15 chars, right at the NVS key length limit.
        snprintf(out, 16, "pn_%02x%02x%02x%02x%02x%02x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    }

    void _rememberPeerName(const esp_bd_addr_t bda, const char *name) {
        if (!name || !name[0]) return;
        char key[16]; _macToKey(bda, key);
        nvs_handle_t handle;
        if (nvs_open("a2dp", NVS_READWRITE, &handle) != ESP_OK) return;
        esp_err_t err = nvs_set_str(handle, key, name);
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err != ESP_OK) ESP_LOGW("BT_SRC", "failed to cache peer name for %s: %s", key, esp_err_to_name(err));
        nvs_close(handle);
    }

    // Drops any pn_* entries in the "a2dp" NVS namespace whose MAC is no
    // longer in the classic-BT bond table. The bond table is the single
    // source of truth - this is purely cache cleanup so stale names from
    // bonds the stack itself evicted (CONFIG_BT_SMP_MAX_BONDS overflow)
    // don't pile up forever. Called on every successful connect rather
    // than only on an actual bond-table change (e.g. AUTH_CMPL_EVT),
    // because neither backend exposes that event to application code
    // without subclassing/reimplementing internals - the sweep itself is
    // cheap enough (a handful of NVS reads in a small namespace) that
    // running it slightly more often than strictly necessary costs
    // nothing that matters.
    void _sweepStalePeerNames() {
        int num = esp_bt_gap_get_bond_device_num();
        // esp_bd_addr_t is a C array type (uint8_t[6]) - not Assignable,
        // so it can't live in a std::vector. A plain heap array is the
        // simplest fit for esp_bt_gap_get_bond_device_list()'s
        // esp_bd_addr_t* out-param.
        esp_bd_addr_t *bonded = nullptr;
        int actual = 0;
        if (num > 0) {
            bonded = new esp_bd_addr_t[num];
            actual = num;
            esp_err_t err = esp_bt_gap_get_bond_device_list(&actual, bonded);
            if (err != ESP_OK) {
                // Don't sweep against a list we couldn't actually fetch -
                // that would erase every cached name, not just the stale
                // ones.
                ESP_LOGW("BT_SRC", "_sweepStalePeerNames: get_bond_device_list failed: %s", esp_err_to_name(err));
                delete[] bonded;
                return;
            }
        }

        nvs_iterator_t it = nullptr;
        esp_err_t find_err = nvs_entry_find(NVS_DEFAULT_PART_NAME, "a2dp", NVS_TYPE_STR, &it);
        std::vector<std::string> stale_keys;
        while (find_err == ESP_OK && it != nullptr) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            if (strncmp(info.key, "pn_", 3) == 0) {
                bool live = false;
                for (int i = 0; i < actual; i++) {
                    char key[16]; _macToKey(bonded[i], key);
                    if (strcmp(key, info.key) == 0) { live = true; break; }
                }
                if (!live) stale_keys.emplace_back(info.key);
            }
            find_err = nvs_entry_next(&it);
        }
        if (it) nvs_release_iterator(it);
        delete[] bonded;

        if (stale_keys.empty()) return;

        nvs_handle_t handle;
        if (nvs_open("a2dp", NVS_READWRITE, &handle) != ESP_OK) return;
        for (const auto &key : stale_keys) {
            nvs_erase_key(handle, key.c_str());
            ESP_LOGI("BT_SRC", "_sweepStalePeerNames: removed stale entry %s", key.c_str());
        }
        nvs_commit(handle);
        nvs_close(handle);
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

    virtual void begin_fast_forward() = 0;
    virtual void begin_rewind() = 0;
    virtual void end_fast_forward() = 0;
    virtual void end_rewind() = 0;

    void beginFastForward() {
        ESP_LOGI("BT_SRC", "begin fast forward");
        begin_fast_forward();
    }

    void beginRewind() {
        ESP_LOGI("BT_SRC", "begin rewind");
        begin_rewind();
    }

    void endFastForward() {
        ESP_LOGI("BT_SRC", "end fast forward");
        end_fast_forward();
    }
    
    void endRewind() {
        ESP_LOGI("BT_SRC", "end rewind");
        end_rewind();
    }
};
