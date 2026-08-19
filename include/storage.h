#pragma once

#include <stdint.h>
#include <string>

/// @brief Platform-agnostic persistent settings storage (NVS on both
/// Arduino and native ESP-IDF) - same reasoning as platform::: a single
/// compile-time-selected implementation, no runtime polymorphism needed.
///
/// init() must be called once, early in setup()/app startup, before any
/// get/set call - and, importantly, before any object that reads settings
/// in its own constructor is constructed. Objects like esPod are typically
/// global and constructed before setup() runs at all, so they must NOT read
/// storage from their constructor - see esPod::loadSettingsFromStorage()
/// for the pattern this exists for: load explicitly, later, once init()
/// has actually run.
namespace storage
{
    void init();

    bool getBool(const char *key, bool defaultValue);
    void setBool(const char *key, bool value);
    int getInt(const char *key, int defaultValue);
    void setInt(const char *key, int value);

    std::string getString(const char *key, const char *defaultValue = "");
    void setString(const char *key, const char *value);
    void setString(const char *key, const std::string &value);
}
