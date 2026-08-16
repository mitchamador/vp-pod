#pragma once

#ifdef ARDUINO
#include "Arduino.h"
#else
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include "esp_types.h"
typedef uint8_t byte;
#endif

#include "esPod_conf.h"

#include "L0x00.h"
#include "L0x03.h"
#include "L0x04.h"
#include "esPod_utils.h"
#include "IUart.h"
#include "platform.h"
#include "IBluetoothPlaybackSource.h"
#include "IBluetoothSourceEvents.h"
#include "storage.h"

namespace SettingsKeys
{
    constexpr const char *SeekAsVolume = "seek_as_vol";
    // Next persisted setting - just another line here, plus one more in
    // esPod::loadSettingsFromStorage().
}

#ifndef IPOD_TAG
#define IPOD_TAG "esPod"
#endif

constexpr uint32_t INVALID_TIMESTAMP = UINT32_MAX;
constexpr uint32_t INVALID_TRACK_NUM = UINT32_MAX;

class IUart;

class esPod : public IBluetoothSourceEvents
{
    friend class L0x00;
    friend class L0x03;
    friend class L0x04;

public:
    // State variables
    bool extendedInterfaceModeActive = false;
    bool disabled = true; // espod starts disabled... it means it keeps flushing the Serial until it is ready to process something

    // Metadata variables
#if TOTAL_NUM_TRACKS == 3
    char fixedPrevTrackTitle[255] = "prev";
    char fixedTrackTitle[255] = "audio track";
    char fixedNextTrackTitle[255] = "next";
    char fixedEmptyTrackTitle[255] = " ";
    char fixedPlaylist[255] = "now playing";

    char fixedAlbumName[255] = "album";
    char fixedTrackGenre[255] = "genre";
    char fixedArtistName[255] = "artist";
    char fixedComposer[255] = "composer";
#endif

    char trackTitle[255] = "title";
    char albumName[255] = "album";
    char artistName[255] = "artist";
#if TOTAL_NUM_TRACKS != 3
    char prevTrackTitle[255] = " ";
    char prevAlbumName[255] = " ";
    char prevArtistName[255] = " ";
#endif
    char trackGenre[255] = "genre";
    char playList[255] = "spotify";
    char composer[255] = "composer";
    uint32_t trackDuration = 1; // Track duration in ms
#if TOTAL_NUM_TRACKS != 3
    uint32_t prevTrackDuration = 1;
#endif
    uint32_t playPosition = 0; // Current playing position of the track in ms
    uint32_t lastPlayPosition = UINT32_MAX; // Last notified playing position of the track in ms

    // Playback Engine
    byte playStatus = PB_STATE_PAUSED;            // Current state of the PBEngine
    byte playStatusNotificationState = NOTIF_OFF; // Current state of the Notifications engine
    byte trackChangeAckPending = 0x00;            // Indicate there is a pending track change.
    uint64_t trackChangeTimestamp = 0;            // Trigger for the last track change request. Time outs the pending track change.
    byte shuffleStatus = 0x00;                    // 00 No Shuffle, 0x01 Tracks 0x02 Albums
    byte repeatStatus = 0x00;                     // 00 Repeat off, 01 One track, 02 All tracks

    // TrackList variables
#if TOTAL_NUM_TRACKS != 3
    uint32_t currentTrackIndex = 0;
    uint32_t prevTrackIndex = TOTAL_NUM_TRACKS - 1; // Starts at the end of the tracklist
#else
    uint32_t currentTrackIndex = INVALID_TRACK_NUM;
#endif

    const uint32_t totalNumberTracks = TOTAL_NUM_TRACKS;
#if TOTAL_NUM_TRACKS != 3
    uint32_t trackList[TOTAL_NUM_TRACKS] = {0};
    uint32_t trackListPosition = INVALID_TRACK_NUM; // Locator for the position of the track ID in the TrackList (of IDS)
#else
    uint32_t trackChangeCompletedTimestamp = INVALID_TIMESTAMP;
    bool _getIndexedPlayingTrackTitleRequested = false;
    bool _firstPbCmdToggle = false;
#endif

    // USE_PEER_NAME
    const char *_peer_name = nullptr;

    // Runtime seek mode (see SEEK_MODE_DEFAULT_VOLUME/SEEK_MODE_TOGGLE_WINDOW_MS
    // in esPod_conf.h) and the hidden double-shuffle-toggle gesture that flips it.
    bool _seekAsVolume = SEEK_MODE_DEFAULT_VOLUME;
    uint32_t _lastShuffleToggleTimestamp = INVALID_TIMESTAMP;

public:
    /// @brief Loads all persisted settings (currently just seek mode) -
    /// call once, explicitly, from setup()/app startup after storage::init()
    /// has run. Deliberately NOT done in the constructor: esPod is normally
    /// a global object, constructed before setup() runs and before NVS
    /// itself is ready - see storage.h.
    void loadSettingsFromStorage();

    /// @brief Sets and persists the seek mode - use this instead of writing
    /// _seekAsVolume directly, so every place that changes it (currently
    /// just the Shuffle gesture in L0x04) doesn't need to remember to save.
    void setSeekAsVolume(bool value);

private:
    // FreeRTOS Queues
    QueueHandle_t _cmdQueue;
    QueueHandle_t _txQueue;
    QueueHandle_t _timerQueue;

    // FreeRTOS tasks (and methods...)
    TaskHandle_t _rxTaskHandle;
    TaskHandle_t _processTaskHandle;
    TaskHandle_t _txTaskHandle;
    TaskHandle_t _timerTaskHandle;

    static void _rxTask(void *pvParameters);
    static void _processTask(void *pvParameters);
    static void _txTask(void *pvParameters);
    static void _timerTask(void *pvParameters); // Add this line

    // FreeRTOS timers for delayed acks
    TimerHandle_t _pendingTimer_0x00;
    TimerHandle_t _pendingTimer_0x03;
    TimerHandle_t _pendingTimer_0x04;

    // Callbacks for each timer
    static void _pendingTimerCallback_0x00(TimerHandle_t xTimer);
    static void _pendingTimerCallback_0x03(TimerHandle_t xTimer);
    static void _pendingTimerCallback_0x04(TimerHandle_t xTimer);

    byte _pendingCmdId_0x00;
    byte _pendingCmdId_0x03;
    byte _pendingCmdId_0x04;

    // FreeRTOS timer and callback for status change notification
    TimerHandle_t _playPositionTimer;
    static void _playPositionTimerCallback(TimerHandle_t xTimer);

    // FreeRTOS timer and callback for PB_CMD
    TimerHandle_t _pbCmdTimer;
    static void _pbCmdTimerCallback(TimerHandle_t xTimer);
    byte _pbCmd;

#define NO_PENDING_STATUS_NOTIFICATION 0xFF
    byte _pendingStatusNotificationCmdId = NO_PENDING_STATUS_NOTIFICATION;

    TimerHandle_t _notificationTimer = nullptr;
    static void _notificationTimerTrampoline(TimerHandle_t xTimer);

    // Serial to the listening device
    IUart &_uart;

    // Packet utilities
    static byte _checksum(const byte *byteArray, uint32_t len);
    void _sendPacket(const byte *byteArray, uint32_t len);
    void _queuePacket(const byte *byteArray, uint32_t len);
    void _queuePacketToFront(const byte *byteArray, uint32_t len);
    void _processPacket(const byte *byteArray, uint32_t len);

    bool _rxIncomplete = false;

    // Device metadata
    const char *_name = ESPIPOD_NAME;
    const uint8_t _SWMajor = 0x01;
    const uint8_t _SWMinor = 0x03;
    const uint8_t _SWrevision = 0x00;
    const char *_serialNumber = "AB345F7HIJK";

    // Outgoing playback commands go through this - set via attachPlaybackSource()
    IBluetoothPlaybackSource *_btSource = nullptr;

    // Applies pending metadata, handling the ack/notification dance.
    void _applyTrackMetadata(const TrackMetadata *pending, byte direction);
    
    // Fallback direction detection for peers that don't fill in
    // track_num/num_tracks meaningfully (observed: always 1/1 on at least
    // one Android AVRCP TG stack) - not guaranteed ordered by the AVRCP
    // spec, but observed to be a simple incrementing index there.
#define INVALID_TRACK_UID UINT64_MAX
    uint64_t trackUid = INVALID_TRACK_UID;
    uint64_t prevTrackUid = INVALID_TRACK_UID; 
    static uint64_t _uidToU64(const uint8_t uid[8]);

public:
    explicit esPod(IUart &uart);
    ~esPod();
    void resetState();

    /// @brief Attaches the Bluetooth backend that outgoing playback
    /// commands (play/pause/next/...) are sent to.
    void attachPlaybackSource(IBluetoothPlaybackSource &btSource);

    uint32_t getPlayPosition();

    // IBluetoothSourceEvents - called by the attached Bluetooth backend
    void onConnectionStateChanged(BtConnectionState state) override;
    void onPeerNameChanged(const char *peerName) override;
    void onPlayStateChanged(BtPlayState state) override;
    void onTrackMetadata(const TrackMetadata &metadata, byte direction) override;
    void onPlayPosition(uint32_t positionMs) override;
    void onTrackChange(uint8_t *uid) override;

    void scheduleNotification(TimerCallbackMessage *msg, uint32_t delay);
    void firePbCmdTimer(uint8_t pbCmd);
 };