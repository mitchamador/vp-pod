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
    virtual void onConnectionStateChanged(BtConnectionState state) = 0;

    /// @brief Peer device name became known (or changed). Fires separately
    /// from onConnectionStateChanged because name resolution is often
    /// asynchronous and resolves noticeably later than the connection
    /// itself (e.g. esp_bt_gap_read_remote_name() on the native backend).
    /// Only called while connected; a disconnect does not trigger this with
    /// an empty name - just rely on onConnectionStateChanged(Disconnected).
    virtual void onPeerNameChanged(const char *peerName) = 0;

    /// @brief Fired right after onConnectionStateChanged(Connected), before
    /// onPeerNameChanged (name resolution is async and can lag). The BD
    /// address is available synchronously at connect time, so esPod uses it
    /// to tell a genuine reconnect of the *same* device apart from a
    /// *different* device connecting while a previous session is still
    /// Suspended.
    virtual void onPeerAddressChanged(const uint8_t bdAddr[6]) = 0;

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

    /// @brief Track change event
    /// @param uid 
    virtual void onTrackChange(uint8_t *uid) = 0;
};
