#pragma once

#include <stdint.h>
#include "esPod_utils.h"

// Connection state of the Bluetooth audio link (A2DP or equivalent)
enum class BtConnectionState
{
    Connecting,
    Connected,
    Disconnected
};

// Playback state as reported by the remote (phone) side
enum class BtPlayState
{
    Playing,
    Paused
};

/// @brief Implemented by esPod (or a thin adapter around it). A Bluetooth
/// backend (IBluetoothPlaybackSource implementation) calls into this
/// interface whenever something relevant happens on the Bluetooth side.
/// esPod never talks to the Bluetooth stack directly - it only reacts to
/// these notifications.
class IBluetoothSourceEvents
{
public:
    virtual ~IBluetoothSourceEvents() = default;

    /// @brief Bluetooth connection state changed.
    /// @param state New connection state.
    /// @param peerName Name of the connected peer device, or nullptr if not
    /// available/not connected. The pointer is only guaranteed to be valid
    /// for the duration of the call.
    virtual void onConnectionStateChanged(BtConnectionState state, const char *peerName) = 0;

    /// @brief Playback (audio) state changed - e.g. the phone started or
    /// paused streaming audio.
    virtual void onPlayStateChanged(BtPlayState state) = 0;

    /// @brief New track metadata is available.
    /// @param metadata Title/artist/album/duration for the (new) current track.
    /// @param direction PB_CMD_NEXT or PB_CMD_PREV, best guess at the
    /// direction of the track change that produced this metadata.
    virtual void onTrackMetadata(const TrackMetadata &metadata, byte direction) = 0;

    /// @brief Current playback position, in milliseconds since the start of
    /// the track. Called periodically (roughly once a second) while playing.
    virtual void onPlayPosition(uint32_t positionMs) = 0;
};
