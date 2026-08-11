#if !defined(ARDUINO)
#include "NativeA2dpBluetoothSource.h"
#include "esPod_conf.h"
#include "platform.h"

#ifdef ZERO_VOLUME_FIX
#include "nvs_flash.h"
#endif

#ifndef AVRC_QUEUE_SIZE
#define AVRC_QUEUE_SIZE 32
#endif
#ifndef PROCESS_AVRC_TASK_STACK_SIZE
#define PROCESS_AVRC_TASK_STACK_SIZE 4096
#endif
#ifndef PROCESS_AVRC_TASK_PRIORITY
#define PROCESS_AVRC_TASK_PRIORITY 6
#endif
#ifndef AVRC_INTERVAL_MS
#define AVRC_INTERVAL_MS 5
#endif
#ifndef ARDUINO_RUNNING_CORE
#define ARDUINO_RUNNING_CORE 1
#endif

namespace
{
    constexpr uint32_t INVALID_TRACK_NUM = UINT32_MAX;
    constexpr uint32_t INVALID_TIMESTAMP = UINT32_MAX;
}

NativeA2dpBluetoothSource *NativeA2dpBluetoothSource::_instance = nullptr;

NativeA2dpBluetoothSource::NativeA2dpBluetoothSource(NativeA2DPSink &a2dpSink, IAudioOutput *audioOutput)
    : _a2dp(a2dpSink), _audioOutput(audioOutput)
{
    _instance = this;
}

void NativeA2dpBluetoothSource::begin(const char *deviceName)
{
    // NativeA2DPSink::init_bluetooth() (called from start()) only sets
    // security params/pin - it deliberately does NOT bring up the BT
    // controller/Bluedroid stack itself (see comment in
    // NativeA2dpBluetoothSink.cpp), that's left to the caller. We do it here,
    // rather than in main.cpp, to keep all A2DP/BT-stack specifics inside
    // this backend. If something else in the app also needs BLE later,
    // this will need to become a shared/centralized init instead.
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE); // classic-only, free BLE memory
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    // BT_CONTROLLER_INIT_CONFIG_DEFAULT() pulls cfg.mode from sdkconfig
    // (CONFIG_BTDM_CTRL_MODE_*), which in an Arduino+ESP-IDF hybrid build
    // commonly defaults to BLE_ONLY. esp_bt_controller_enable() requires the
    // requested mode to be a subset of what the controller was initialized
    // with, so without this override enable(CLASSIC_BT) fails with
    // ESP_ERR_INVALID_ARG regardless of what mode we ask to enable.
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE("BT_SRC", "esp_bt_controller_init failed: %s", esp_err_to_name(err));
    }
    err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE("BT_SRC", "esp_bt_controller_enable failed: %s", esp_err_to_name(err));
    }
    err = esp_bluedroid_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE("BT_SRC", "esp_bluedroid_init failed: %s", esp_err_to_name(err));
    }
    err = esp_bluedroid_enable();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE("BT_SRC", "esp_bluedroid_enable failed: %s", esp_err_to_name(err));
    }

    _avrcMetadataQueue = xQueueCreate(AVRC_QUEUE_SIZE, sizeof(AvrcMetadataItem));
    if (_avrcMetadataQueue == nullptr)
    {
        ESP_LOGE("BT_SRC", "Failed to create AVRC metadata queue");
        esp_restart();
    }

    xTaskCreatePinnedToCore(_processAvrcTask, "processAVRCTask", PROCESS_AVRC_TASK_STACK_SIZE, this,
                             PROCESS_AVRC_TASK_PRIORITY, &_processAvrcTaskHandle, ARDUINO_RUNNING_CORE);
    if (_processAvrcTaskHandle == nullptr)
    {
        ESP_LOGE("BT_SRC", "Failed to create processAVRCTask");
        esp_restart();
    }

    // NativeA2DPSink::set_auto_reconnect() takes a retry *count*, not a
    // delay in ms (unlike BluetoothA2DPSink) - deliberately not reusing the
    // "10000" from the ESP32-A2DP backend here, that would mean 10000
    // retries.
    _a2dp.set_auto_reconnect(true, 10000);
    _a2dp.set_on_connection_state_changed(_connectionStateChangedTrampoline);
    _a2dp.set_on_audio_state_changed(_audioStateChangedTrampoline);
    _a2dp.set_avrc_connection_state_callback(_avrcConnectionStateTrampoline);
    _a2dp.set_avrc_metadata_callback(_avrcMetadataTrampoline);
    _a2dp.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
                                            ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_PLAYING_TIME |
                                            ESP_AVRC_MD_ATTR_TRACK_NUM | ESP_AVRC_MD_ATTR_NUM_TRACKS);
    _a2dp.set_avrc_rn_play_pos_callback(_avrcPlayPosTrampoline, 1);
#ifdef ZERO_VOLUME_FIX
    _a2dp.set_avrc_rn_volumechange(_avrcVolumeChangeTrampoline);
    _a2dp.set_avrc_rn_volumechange_completed(_avrcVolumeChangeCompletedTrampoline);
#endif
    _a2dp.set_stream_reader(_streamReaderTrampoline, false);
    _a2dp.set_codec_config_callback(_codecConfigTrampoline);
#ifdef TRACK_CHANGE_CALLBACK
    _a2dp.set_avrc_rn_track_change_callback(_avrcTrackChangeTrampoline);
#endif
#ifdef USE_PEER_NAME
    _a2dp.set_on_peer_name_available(_peerNameAvailableTrampoline);
#endif
    _a2dp.start(deviceName);

    ESP_LOGI("BT_SRC", "NativeA2DPSink started: %s", deviceName);
    platform::delay_ms(500);
    _a2dp.set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);
}

void NativeA2dpBluetoothSource::next()
{
    _a2dp.next();
#ifdef TRACK_POSITION_FIX
    _resetPlayPositionEstimate();
#endif
}

void NativeA2dpBluetoothSource::previous()
{
    _a2dp.previous();
#ifdef TRACK_POSITION_FIX
    _resetPlayPositionEstimate();
#endif
}

bool NativeA2dpBluetoothSource::isConnected() const
{
    return _a2dp.get_connection_state() == ESP_A2D_CONNECTION_STATE_CONNECTED;
}

void NativeA2dpBluetoothSource::forgetConnection()
{
    if (_a2dp.get_connection_state() != ESP_A2D_CONNECTION_STATE_DISCONNECTED)
    {
        _a2dp.disconnect();
    }
    _a2dp.clean_last_connection();
}

//-----------------------------------------------------------------------
//|                      AVRC metadata queue/task                       |
//-----------------------------------------------------------------------

void NativeA2dpBluetoothSource::_filterPayload(char *output, const char *input)
{
    size_t in_idx = 0, out_idx = 0, max_len = 255;
    while (input[in_idx] != '\0' && out_idx < max_len - 1)
    {
        unsigned char c = input[in_idx];

        int char_len = 1;
        if (c >= 0xF0)
            char_len = 4;
        else if (c >= 0xE0)
            char_len = 3;
        else if (c >= 0xC0)
            char_len = 2;

        if (char_len >= 4 || (char_len == 3 && c >= 0xEF))
        {
            in_idx += char_len;
        }
        else
        {
            for (int i = 0; i < char_len && input[in_idx + i] != '\0'; i++)
            {
                output[out_idx++] = input[in_idx++];
            }
        }
    }
    output[out_idx] = '\0';
}

void NativeA2dpBluetoothSource::_processAvrcTask(void *pvParameters)
{
    static_cast<NativeA2dpBluetoothSource *>(pvParameters)->_runProcessAvrcTask();
}

void NativeA2dpBluetoothSource::_runProcessAvrcTask()
{
    AvrcMetadataItem incMetadata;

    TrackMetadata pendingMetadata;

    uint32_t trackNum = INVALID_TRACK_NUM, prevTrackNum = INVALID_TRACK_NUM;
    uint32_t avrcMetadataTimestamp = INVALID_TIMESTAMP;

    while (true)
    {
        if (xQueueReceive(_avrcMetadataQueue, &incMetadata, 0) == pdTRUE)
        {
            avrcMetadataTimestamp = platform::time_now_ms();

            switch (incMetadata.id)
            {
            case ESP_AVRC_MD_ATTR_TRACK_NUM:
                trackNum = strtoul((const char *)incMetadata.payload, nullptr, 10);
                if (prevTrackNum == INVALID_TRACK_NUM)
                    prevTrackNum = trackNum;
                break;
            case ESP_AVRC_MD_ATTR_TITLE:
                _filterPayload(pendingMetadata.title, (char *)incMetadata.payload);
#ifdef TRACK_POSITION_FIX
                _resetPlayPositionEstimate();
#endif
                break;
            case ESP_AVRC_MD_ATTR_ALBUM:
                _filterPayload(pendingMetadata.album, (char *)incMetadata.payload);
                break;
            case ESP_AVRC_MD_ATTR_ARTIST:
                _filterPayload(pendingMetadata.artist, (char *)incMetadata.payload);
                break;
            case ESP_AVRC_MD_ATTR_PLAYING_TIME:
                pendingMetadata.duration = strtoul((const char *)incMetadata.payload, nullptr, 10);
                break;
            }
            delete[] incMetadata.payload;
            incMetadata.payload = nullptr;
        }
        if (avrcMetadataTimestamp != INVALID_TIMESTAMP && platform::time_now_ms() - avrcMetadataTimestamp > AVRC_RECEIVE_METADATA_TIMEOUT)
        {
            avrcMetadataTimestamp = INVALID_TIMESTAMP;

            if (_sink != nullptr)
            {
                _sink->onTrackMetadata(pendingMetadata, trackNum < prevTrackNum ? PB_CMD_PREV : PB_CMD_NEXT);
            }

            prevTrackNum = trackNum;
        }
        vTaskDelay(pdMS_TO_TICKS(AVRC_INTERVAL_MS));
    }
}

//-----------------------------------------------------------------------
//|                 NativeA2DPSink callback trampolines                 |
//-----------------------------------------------------------------------

void NativeA2dpBluetoothSource::_connectionStateChangedTrampoline(esp_a2d_connection_state_t state, void *ptr)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    switch (state)
    {
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
        ESP_LOGI("BT_SRC", "ESP_A2D_CONNECTION_STATE_CONNECTING");
        if (self->_sink != nullptr)
        {
            self->_sink->onConnectionStateChanged(BtConnectionState::Connecting);
        }
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
        ESP_LOGI("BT_SRC", "ESP_A2D_CONNECTION_STATE_CONNECTED");
#ifdef ZERO_VOLUME_FIX
        self->_volumeState = VolumeState::AfterConnectionNotDefined;
#endif
        if (self->_sink != nullptr)
        {
            self->_sink->onConnectionStateChanged(BtConnectionState::Connected);
#ifdef USE_PEER_NAME
            self->_sink->onPeerNameChanged(self->_a2dp.get_peer_name());
#endif
        }
        break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        ESP_LOGI("BT_SRC", "ESP_A2D_CONNECTION_STATE_DISCONNECTED");
        if (self->_audioOutput != nullptr)
        {
            self->_audioOutput->stop(); // stop stale DMA buffer content looping with no new audio incoming
        }
        if (self->_sink != nullptr)
        {
            self->_sink->onConnectionStateChanged(BtConnectionState::Disconnected);
        }
        break;
    default:
        break; 
    }
}

void NativeA2dpBluetoothSource::_audioStateChangedTrampoline(esp_a2d_audio_state_t state, void *ptr)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    switch (state)
    {
    case ESP_A2D_AUDIO_STATE_STARTED:
#ifdef ZERO_VOLUME_FIX
        if (self->_volumeState == VolumeState::AfterConnectionNotDefined)
        {
            self->_volumeState = VolumeState::AfterConnectionSet;
            ESP_LOGI("BT_SRC", "Volume is not set after connecting. Restore volume to saved value");
            self->_a2dp.set_volume(_nvsGetVolume());
        }
#endif
        ESP_LOGI("BT_SRC", "ESP_A2D_AUDIO_STATE_STARTED");
        if (self->_sink != nullptr)
        {
            self->_sink->onPlayStateChanged(BtPlayState::Playing);
        }
        break;
    case ESP_A2D_AUDIO_STATE_SUSPEND:
        ESP_LOGI("BT_SRC", "ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND");
        if (self->_sink != nullptr)
        {
            self->_sink->onPlayStateChanged(BtPlayState::Paused);
        }
#ifdef ZERO_VOLUME_FIX
        _nvsSetVolume(self->_a2dp.get_volume());
#endif
        break;
    }

#ifdef TRACK_POSITION_FIX
    if (state == ESP_A2D_AUDIO_STATE_STARTED)
    {
        self->_isPlaying = true;
    }
    else if (state == ESP_A2D_AUDIO_STATE_STOPPED)
    {
        self->_isPlaying = false;
        self->_resetPlayPositionEstimate();
    }
    else // ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND (Paused)
    {
        self->_isPlaying = false;
    }
#endif
}

void NativeA2dpBluetoothSource::_avrcConnectionStateTrampoline(bool connected)
{
    ESP_LOGI("BT_SRC", "AVRC Connection state: %s", connected ? "connected" : "disconnected");
}

void NativeA2dpBluetoothSource::_avrcMetadataTrampoline(uint8_t id, const uint8_t *text)
{
    auto *self = _instance;
    if (self == nullptr || text == nullptr)
        return;

    // text is only valid for the duration of this call (owned by
    // NativeA2DPSink, freed right after) - copy it before queueing, same
    // reasoning as the metadata copy inside NativeA2dpBluetoothSink.cpp itself.
    AvrcMetadataItem incMetadata;
    incMetadata.id = id;
    incMetadata.payload = new uint8_t[255];
    strncpy((char *)incMetadata.payload, (const char *)text, 254);
    incMetadata.payload[254] = '\0';
    if (xQueueSend(self->_avrcMetadataQueue, &incMetadata, 0) != pdTRUE)
    {
        ESP_LOGW("BT_SRC", "Metadata queue full, discarding metadata");
        delete[] incMetadata.payload;
        incMetadata.payload = nullptr;
    }
}

void NativeA2dpBluetoothSource::_avrcPlayPosTrampoline(uint32_t playPos)
{
    auto *self = _instance;
    if (self == nullptr || self->_sink == nullptr)
        return;

    ESP_LOGI("BT_SRC", "PlayPosition called");

#ifdef TRACK_POSITION_FIX
    self->_lastRealPositionTimestamp = platform::time_now_ms();
#endif
    self->_sink->onPlayPosition(playPos);
}

#ifdef ZERO_VOLUME_FIX

uint8_t NativeA2dpBluetoothSource::_nvsGetVolume()
{
    uint8_t volume;
    nvs_handle_t h;

    if (nvs_open(ESPIPOD_NAME, NVS_READWRITE, &h) == ESP_OK)
    {
        if (nvs_get_u8(h, "volume", &volume) != ESP_OK)
        {
            ESP_LOGE("BT_SRC", "failed to get volume from NVS");
        } else {
            ESP_LOGI("BT_SRC", "get volume from NVS: %d", volume);
        }
        nvs_close(h);
    }
    return volume;
}

void NativeA2dpBluetoothSource::_nvsSetVolume(uint8_t volume)
{
    if (volume != _nvsGetVolume())
    {
        nvs_handle_t h;

        if (nvs_open(ESPIPOD_NAME, NVS_READWRITE, &h) == ESP_OK)
        {
            if (nvs_set_u8(h, "volume", volume) != ESP_OK)
            {
                ESP_LOGE("BT_SRC", "failed to save volume to NVS");
            } else {
                if (nvs_commit(h) != ESP_OK)
                {
                    ESP_LOGE("BT_SRC", "failed to commit NVS");
                } else {
                    ESP_LOGI("BT_SRC", "set volume to NVS: %d", volume);
                }
            }
            nvs_close(h);
        }
    }
}

void NativeA2dpBluetoothSource::_avrcVolumeChangeTrampoline(int volume)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    ESP_LOGD("BT_SRC", "Volume change: %d%%", (int)volume * 100 / 0x7f);
    if (self->_volumeState == VolumeState::AfterConnectionNotDefined)
    {
        self->_volumeState = VolumeState::AfterConnectionSet;
        if (volume == 0)
        {
            uint8_t saved_volume = _nvsGetVolume();
            if (volume != saved_volume)
            {
                ESP_LOGI("BT_SRC", "Volume is set to 0 after connecting. Restore volume to saved value");
                self->_a2dp.set_volume(saved_volume);
            }
        }
    }
}

void NativeA2dpBluetoothSource::_avrcVolumeChangeCompletedTrampoline(int volume)
{
    ESP_LOGD("BT_SRC", "Volume change completed: %d%%", (int)volume * 100 / 0x7f);
}

#endif

void NativeA2dpBluetoothSource::_streamReaderTrampoline(const uint8_t *data, uint32_t length)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    if (self->_audioOutput != nullptr)
    {
        self->_audioOutput->write(data, length);
    }

#ifdef TRACK_POSITION_FIX
    if (self->_isPlaying)
    {
        self->_rawAudioDataBytesReceived += length;
        if (self->_rawAudioDataBytesReceived > self->_prevRawAudioDataBytesReceived + BYTES_POSITION_HUNK)
        {
            self->_prevRawAudioDataBytesReceived = self->_rawAudioDataBytesReceived;

            bool realPositionRecent = self->_lastRealPositionTimestamp != UINT32_MAX &&
                                       (platform::time_now_ms() - self->_lastRealPositionTimestamp) < REAL_POSITION_STALE_MS;

            if (!realPositionRecent && self->_sink != nullptr)
            {
                uint32_t estimatedPositionMs = (uint32_t)(self->_rawAudioDataBytesReceived / (BYTES_PER_SECOND / 1000.0));
                if (estimatedPositionMs > 500)
                {
                    self->_sink->onPlayPosition(estimatedPositionMs);
                }
            }
        }
    }
#endif
}

void NativeA2dpBluetoothSource::_codecConfigTrampoline(uint32_t rate, uint8_t bps, uint8_t channels)
{
    auto *self = _instance;
    if (self == nullptr || self->_audioOutput == nullptr)
        return;

    ESP_LOGI("BT_SRC", "Codec config: %uHz, %ubit, %uch", rate, bps, channels);
    self->_audioOutput->begin(rate, bps, channels);
}

#ifdef TRACK_CHANGE_CALLBACK
void NativeA2dpBluetoothSource::_avrcTrackChangeTrampoline(uint8_t *uid)
{
    ESP_LOGI("BT_SRC",
             "Track UID: %02X%02X%02X%02X%02X%02X%02X%02X",
             uid[0], uid[1], uid[2], uid[3],
             uid[4], uid[5], uid[6], uid[7]);
}
#endif

#ifdef USE_PEER_NAME
void NativeA2dpBluetoothSource::_peerNameAvailableTrampoline(const char *name)
{
    auto *self = _instance;
    if (self == nullptr || self->_sink == nullptr)
        return;

    // The remote-name lookup is async and typically resolves well after the
    // connection itself. Guard against a stale/late callback arriving after
    // we've since disconnected.
    if (self->isConnected())
    {
        self->_sink->onPeerNameChanged(name);
    }
}
#endif

#endif