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

    char fixedAlbumName[255] = "Album";
    char fixedTrackGenre[255] = "Genre";
    char fixedArtistName[255] = "Artist";
    char fixedComposer[255] = "Composer";
#endif

    char trackTitle[255] = "Title";
    char albumName[255] = "Album";
    char artistName[255] = "Artist";
#if TOTAL_NUM_TRACKS != 3
    char prevTrackTitle[255] = " ";
    char prevAlbumName[255] = " ";
    char prevArtistName[255] = " ";
#endif
    char trackGenre[255] = "Genre";
    char playList[255] = "Spotify";
    char composer[255] = "Composer";
    uint32_t trackDuration = 1; // Track duration in ms
#if TOTAL_NUM_TRACKS != 3
    uint32_t prevTrackDuration = 1;
#endif
    uint32_t playPosition = 0; // Current playing position of the track in ms

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
#endif

#ifdef STATUS_NOTIFICATION_QUEUE
    QueueHandle_t _statusChangeNotificationTimerQueue;
#endif

#ifdef USE_PEER_NAME
    const char *_peer_name = nullptr;
#endif

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
#ifdef STATUS_NOTIFICATION_QUEUE
    TaskHandle_t _statusChangeNotificationTimerTaskHandle;
#endif

    static void _rxTask(void *pvParameters);
    static void _processTask(void *pvParameters);
    static void _txTask(void *pvParameters);
    static void _timerTask(void *pvParameters); // Add this line
#ifdef STATUS_NOTIFICATION_QUEUE
    static void _statusChangeNotificationTimerTask(void *pvParameters);
#endif

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

#ifdef STATUS_NOTIFICATION_QUEUE
    // FreeRTOS timer for status change notification
    TimerHandle_t _statusChangeNotificationTimer;
    // callback
    static void _statusChangeNotificationTimerCallback(TimerHandle_t xTimer);
#endif

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
 };