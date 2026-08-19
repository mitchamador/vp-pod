#include "storage.h"
#include <string>

#ifdef ARDUINO
#include "ArduinoNvs.h"

namespace storage
{
    void init()
    {
        NVS.begin();
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

    int getInt(const char *key, int defaultValue)
    {
        // ArduinoNvs has no getBool/setBool in every version - store as 0/1
        // via getInt/setInt instead, which is always available.
        return NVS.getInt(key, defaultValue);
    }

    void setInt(const char *key, int value)
    {
        if (!NVS.setInt(key, (int16_t) value, true))
        {
            ESP_LOGE("storage", "failed to save %s to NVS", key);
        }
    }

    std::string getString(const char *key, const char *defaultValue)
    {
        String value = NVS.getString(key);
        if (value.length() == 0)
        {
            return defaultValue ? defaultValue : "";
        }
        return std::string(value.c_str());
    }

    void setString(const char *key, const char *value)
    {
        if (!value)
        {
            value = "";
        }
        if (!NVS.setString(key, String(value)))
        {
            ESP_LOGE("storage", "failed to save %s to NVS", key);
        }
    }

    void setString(const char *key, const std::string &value)
    {
        setString(key, value.c_str());
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

    int getInt(const char *key, int defaultValue)
    {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        {
            return defaultValue;
        }
        int16_t value = defaultValue;
        esp_err_t err = nvs_get_i16(handle, key, &value);
        nvs_close(handle);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE("storage", "failed to read %s from NVS: %s", key, esp_err_to_name(err));
        }
        return value;
    }

    void setInt(const char *key, int value)
    {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        {
            ESP_LOGE("storage", "failed to open NVS to save %s", key);
            return;
        }
        esp_err_t err = nvs_set_i16(handle, key, value);
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

    std::string getString(const char *key, const char *defaultValue)
    {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        {
            return defaultValue ? defaultValue : "";
        }

        size_t required_size = 0;
        esp_err_t err = nvs_get_str(handle, key, nullptr, &required_size);

        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            nvs_close(handle);
            return defaultValue ? defaultValue : "";
        }

        if (err != ESP_OK)
        {
            ESP_LOGE("storage", "failed to get size of %s from NVS: %s", key, esp_err_to_name(err));
            nvs_close(handle);
            return defaultValue ? defaultValue : "";
        }

        std::string result(required_size, '\0');
        err = nvs_get_str(handle, key, &result[0], &required_size);
        nvs_close(handle);

        if (err != ESP_OK)
        {
            ESP_LOGE("storage", "failed to read %s from NVS: %s", key, esp_err_to_name(err));
            return defaultValue ? defaultValue : "";
        }

        // nvs_get_str включает завершающий '\0' в required_size
        if (!result.empty() && result.back() == '\0')
        {
            result.pop_back();
        }

        return result;
    }

    void setString(const char *key, const char *value)
    {
        if (!value)
        {
            value = "";
        }

        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        {
            ESP_LOGE("storage", "failed to open NVS to save %s", key);
            return;
        }

        esp_err_t err = nvs_set_str(handle, key, value);
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

    void setString(const char *key, const std::string &value)
    {
        setString(key, value.c_str());
    }
}

#endif
