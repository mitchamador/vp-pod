#ifdef ARDUINO
#include "Esp32A2dpBluetoothSource.h"
#include "esPod_conf.h"
#include "platform.h"
#include <cstring>

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

namespace
{
    constexpr uint32_t INVALID_TRACK_NUM = UINT32_MAX;
    constexpr uint32_t INVALID_TIMESTAMP = UINT32_MAX;
}

Esp32A2dpBluetoothSource *Esp32A2dpBluetoothSource::_instance = nullptr;

Esp32A2dpBluetoothSource::Esp32A2dpBluetoothSource(BluetoothA2DPSink &a2dpSink, IAudioOutput *audioOutput)
    : _a2dp(a2dpSink), _audioOutput(audioOutput)
{
    _instance = this;
}

void Esp32A2dpBluetoothSource::begin(const char *deviceName)
{
    if (_audioOutput != nullptr)
    {
        // No codec-change callback available on this path (see header
        // comment) - SBC via BluetoothA2DPSink's manual stream reader is
        // always 44100/16/2 in practice, so a fixed begin() is fine here.
        _audioOutput->begin(44100, 16, 2);
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

    _a2dp.set_auto_reconnect(true, 10000);
    _a2dp.set_on_connection_state_changed(_connectionStateChangedTrampoline);
    _a2dp.set_on_audio_state_changed(_audioStateChangedTrampoline);
    _a2dp.set_avrc_connection_state_callback(_avrcConnectionStateTrampoline);
    _a2dp.set_avrc_metadata_callback(_avrcMetadataTrampoline);
    _a2dp.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
                                            ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_PLAYING_TIME |
                                            ESP_AVRC_MD_ATTR_TRACK_NUM | ESP_AVRC_MD_ATTR_NUM_TRACKS);
    _a2dp.set_avrc_rn_play_pos_callback(_avrcPlayPosTrampoline, 1);
    _a2dp.set_avrc_rn_volumechange(_avrcVolumeChangeTrampoline);
    _a2dp.set_avrc_rn_volumechange_completed(_avrcVolumeChangeCompletedTrampoline);
    _a2dp.set_stream_reader(_readDataStreamTrampoline, false);
    _a2dp.set_avrc_rn_track_change_callback(_avrcTrackChangeTrampoline);
    _a2dp.start(deviceName);

    ESP_LOGI("BT_SRC", "a2dp_sink started: %s", deviceName);
    platform::delay_ms(500);
    _a2dp.set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);
}

void Esp32A2dpBluetoothSource::next()
{
    _a2dp.next();
    if (_audioOutput != nullptr)
        _audioOutput->_resetPlayPositionEstimate();
}

void Esp32A2dpBluetoothSource::previous()
{
    _a2dp.previous();
    if (_audioOutput != nullptr)
        _audioOutput->_resetPlayPositionEstimate();
}

bool Esp32A2dpBluetoothSource::isConnected() const
{
    return _a2dp.get_connection_state() == ESP_A2D_CONNECTION_STATE_CONNECTED;
}

void Esp32A2dpBluetoothSource::forgetConnection()
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

void Esp32A2dpBluetoothSource::_filterPayload(char *output, const char *input)
{
    size_t in_idx = 0, out_idx = 0, max_len = 255;
    while (input[in_idx] != '\0' && out_idx < max_len - 1)
    {
        unsigned char c = input[in_idx];

        // Check for UTF-8 byte length based on leading bits
        int char_len = 1;
        if (c >= 0xF0)
        {
            char_len = 4; // 4-byte characters (most emojis live here)
        }
        else if (c >= 0xE0)
        {
            char_len = 3; // 3-byte characters (some symbols/emojis)
        }
        else if (c >= 0xC0)
        {
            char_len = 2; // 2-byte characters
        }

        // If it's a 4-byte or high 3-byte sequence, skip it (treat as emoji/unsupported symbol)
        if (char_len >= 4 || (char_len == 3 && c >= 0xEF))
        {
            in_idx += char_len;
        }
        else
        {
            // Copy valid standard bytes
            for (int i = 0; i < char_len && input[in_idx + i] != '\0'; i++)
            {
                output[out_idx++] = input[in_idx++];
            }
        }
    }
    output[out_idx] = '\0';
}

void Esp32A2dpBluetoothSource::_processAvrcTask(void *pvParameters)
{
    static_cast<Esp32A2dpBluetoothSource *>(pvParameters)->_runProcessAvrcTask();
}

void Esp32A2dpBluetoothSource::_runProcessAvrcTask()
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
                trackNum = String((char *)incMetadata.payload).toInt();
                if (prevTrackNum == INVALID_TRACK_NUM)
                    prevTrackNum = trackNum;
                break;
            case ESP_AVRC_MD_ATTR_TITLE:
                _filterPayload(pendingMetadata.title, (char *)incMetadata.payload);
                if (_audioOutput != nullptr)
                    _audioOutput->_resetPlayPositionEstimate();
                break;
            case ESP_AVRC_MD_ATTR_ALBUM:
                _filterPayload(pendingMetadata.album, (char *)incMetadata.payload);
                break;
            case ESP_AVRC_MD_ATTR_ARTIST:
                _filterPayload(pendingMetadata.artist, (char *)incMetadata.payload);
                break;
            case ESP_AVRC_MD_ATTR_PLAYING_TIME:
                pendingMetadata.duration = String((char *)incMetadata.payload).toInt();
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
                _sink->onTrackMetadata(pendingMetadata, trackNum < prevTrackNum ? BROWSE_DIRECTION_PREV
                                                      : (trackNum > prevTrackNum ? BROWSE_DIRECTION_NEXT
                                                                                 : BROWSE_DIRECTION_NONE));
            }

            prevTrackNum = trackNum;
        }
        vTaskDelay(pdMS_TO_TICKS(AVRC_INTERVAL_MS));
    }
}

void Esp32A2dpBluetoothSource::doSetVolume(uint8_t volume) {
    _a2dp.set_volume(volume);
}

uint8_t Esp32A2dpBluetoothSource::doGetVolume() {
    return _a2dp.get_volume();
}

//-----------------------------------------------------------------------
//|                  ESP32-A2DP callback trampolines                    |
//-----------------------------------------------------------------------

void Esp32A2dpBluetoothSource::_connectionStateChangedTrampoline(esp_a2d_connection_state_t state, void *ptr)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    switch (state)
    {
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
        self->_connectionState = state;
        ESP_LOGI("BT_SRC", "ESP_A2D_CONNECTION_STATE_CONNECTING");
        if (self->_sink != nullptr)
        {
            self->_sink->onConnectionStateChanged(BtConnectionState::Connecting);
        }
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
        self->_connectionState = state;
        ESP_LOGI("BT_SRC", "ESP_A2D_CONNECTION_STATE_CONNECTED");

        self->_onConnected();

        if (self->_sink != nullptr)
        {
            self->_sink->onConnectionStateChanged(BtConnectionState::Connected);
            esp_bd_addr_t *bda = self->_a2dp.get_current_peer_address();
            if (bda != nullptr)
                self->_sink->onPeerAddressChanged(*bda);
            self->_sink->onPeerNameChanged(self->_a2dp.get_peer_name());
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
        if (self->_connectionState == ESP_A2D_CONNECTION_STATE_CONNECTING) // no connected state -> reconnect
        {
            self->_a2dp.reconnect();
        }
        self->_connectionState = state;
        break;
    default:
        break;
    }
}

void Esp32A2dpBluetoothSource::_audioStateChangedTrampoline(esp_a2d_audio_state_t state, void *ptr)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    switch (state)
    {
    case ESP_A2D_AUDIO_STATE_STARTED:
        ESP_LOGI("BT_SRC", "ESP_A2D_AUDIO_STATE_STARTED");
        self->_restoreVolume(INVALID_VOLUME);
        if (self->_sink != nullptr)
            self->_sink->onPlayStateChanged(BtPlayState::Playing);
        break;
    case ESP_A2D_AUDIO_STATE_SUSPEND:
        ESP_LOGI("BT_SRC", "ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND");
        if (self->_sink != nullptr)
            self->_sink->onPlayStateChanged(BtPlayState::Paused);
        self->_saveVolume();
        break;
    }

    if (self->_audioOutput != nullptr)
        self->_audioOutput->_updatePlayingState(state == ESP_A2D_AUDIO_STATE_STARTED ? PLAY_STATUS_PLAY :
                                               (state == ESP_A2D_AUDIO_STATE_STOPPED ? PLAY_STATUS_STOP :
                                                                                       PLAY_STATUS_PAUSE));

}

void Esp32A2dpBluetoothSource::_avrcConnectionStateTrampoline(bool connected)
{
    ESP_LOGD("BT_SRC", "AVRC Connection state: %s", connected ? "connected" : "disconnected");
}

void Esp32A2dpBluetoothSource::_avrcMetadataTrampoline(uint8_t id, const uint8_t *text)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    AvrcMetadataItem incMetadata;
    incMetadata.id = id;
    incMetadata.payload = new uint8_t[255];
    memcpy(incMetadata.payload, text, 255);
    if (xQueueSend(self->_avrcMetadataQueue, &incMetadata, 0) != pdTRUE)
    {
        ESP_LOGW("BT_SRC", "Metadata queue full, discarding metadata");
        delete[] incMetadata.payload;
        incMetadata.payload = nullptr;
    }
}

void Esp32A2dpBluetoothSource::_avrcPlayPosTrampoline(uint32_t playPos)
{
    auto *self = _instance;
    if (self == nullptr || self->_sink == nullptr)
        return;

    ESP_LOGD("BT_SRC", "PlayPosition called");

    if (self->_audioOutput != nullptr)
        self->_audioOutput->_updateLastRealPositionTimestamp();
    self->_sink->onPlayPosition(playPos);
}

void Esp32A2dpBluetoothSource::_avrcVolumeChangeTrampoline(int volume)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

    ESP_LOGD("BT_SRC", "Volume change: %d%%", (int)volume * 100 / 0x7f);

    self->_restoreVolume(volume);
}

void Esp32A2dpBluetoothSource::_avrcVolumeChangeCompletedTrampoline(int volume)
{
    ESP_LOGD("BT_SRC", "Volume change completed: %d%%", (int)volume * 100 / 0x7f);
}

void Esp32A2dpBluetoothSource::_readDataStreamTrampoline(const uint8_t *data, uint32_t length)
{
    auto *self = _instance;
    if (self == nullptr)
        return;

#ifndef AUDIOKIT
    if (self->_audioOutput != nullptr)
    {
        self->_audioOutput->write(data, length);
        uint32_t estimatedPositionMs = self->_audioOutput->_getEstimatedPosition();
        if (estimatedPositionMs > 0) {
            self->_sink->onPlayPosition(estimatedPositionMs);
        }
    }
#endif
}

void Esp32A2dpBluetoothSource::_avrcTrackChangeTrampoline(uint8_t *uid)
{
    auto *self = _instance;
    if (self == nullptr)
        return;
    if (self->_sink != nullptr)
        self->_sink->onTrackChange(uid);
}

#endif