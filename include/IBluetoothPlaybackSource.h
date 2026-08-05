#pragma once

class IBluetoothSourceEvents;

/// @brief Implemented by a Bluetooth backend (e.g. Esp32A2dpBluetoothSource,
/// or later a UART-connected BT module). esPod calls into this interface to
/// control playback on the remote (phone) side - it never talks to a
/// specific Bluetooth stack (A2DP/AVRCP, vendor AT commands, ...) directly.
class IBluetoothPlaybackSource
{
public:
    virtual ~IBluetoothPlaybackSource() = default;

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

    virtual bool isConnected() const = 0;

    /// @brief Disconnects the current peer (if any) and forgets it, so the
    /// next connection attempt starts fresh rather than auto-reconnecting.
    /// Used e.g. by a physical "reset pairing" button.
    virtual void forgetConnection() = 0;
};
