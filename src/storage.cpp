#include "storage.h"

#ifdef ARDUINO
#include "ArduinoNvs.h"

namespace storage
{
    void init()
    {
        NVS.begin(); // safe to call again even if ZERO_VOLUME_FIX already did
    }

    bool getBool(const char *key, bool defaultValue)
    {
        // ArduinoNvs has no getBool/setBool in every version - store as 0/1
        // via getInt/setInt instead, which is always available.
        return NVS.getInt(key, defaultValue ? 1 : 0) != 0;
    }

    void setBool(const char *key, bool value)
    {
        if (!NVS.setInt(key, (uint8_t) (value ? 1 : 0), true))
        {
            ESP_LOGE("storage", "failed to save %s to NVS", key);
        }
    }
}

#else
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

namespace
{
    constexpr const char *NVS_NAMESPACE = "settings";
}

namespace storage
{
    void init()
    {
        // NVS init
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ESP_ERROR_CHECK(nvs_flash_init());
        }
    }

    bool getBool(const char *key, bool defaultValue)
    {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        {
            return defaultValue;
        }
        uint8_t value = defaultValue ? 1 : 0;
        esp_err_t err = nvs_get_u8(handle, key, &value);
        nvs_close(handle);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE("storage", "failed to read %s from NVS: %s", key, esp_err_to_name(err));
        }
        return value != 0;
    }

    void setBool(const char *key, bool value)
    {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        {
            ESP_LOGE("storage", "failed to open NVS to save %s", key);
            return;
        }
        esp_err_t err = nvs_set_u8(handle, key, value ? 1 : 0);
        if (err == ESP_OK)
        {
            err = nvs_commit(handle);
        }
        if (err != ESP_OK)
        {
            ESP_LOGE("storage", "failed to save %s to NVS: %s", key, esp_err_to_name(err));
        }
        nvs_close(handle);
    }
}

#endif
