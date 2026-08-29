#pragma once

#include "storage.h"

namespace SettingsKeys
{
    // "seek_as_vol" (bool) is retired - esPod::SeekMode is now an int,
    // and NVS doesn't allow reusing a key under a different value type, so
    // this is a new key rather than a migration.
    constexpr const char *SeekMode = "seek_mode";
    constexpr const char *TrackPositionFix = "track_pos_fix";
    constexpr const char *ZeroVolumeFix = "zero_vol_fix";
    constexpr const char *Volume = "volume";
    constexpr const char *UsePeerName = "use_peer_name";
    constexpr const char *esPodName = "espod_name";
    constexpr const char *SuspendTimeoutSec = "suspend_timeout_s";
}
