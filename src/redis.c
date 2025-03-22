#include "redis.h"

// redis 配置
redis_config_t r_config = nullptr;

/**
 * 讀取redis配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_redis_config(GKeyFile* keyfile, GError* error)
{
    // 創建 redis 配置對象
    r_config = g_malloc0(sizeof(redis_config));
    r_config->redis_host = nullptr;
    r_config->redis_username = nullptr;
    r_config->redis_password = nullptr;

    // 讀取檢查間隔秒數
    error = nullptr;
    r_config->interval_seconds = g_key_file_get_integer(keyfile, "General", "interval", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading interval: %s\n", error->message);
        goto error;
    }

    // 讀取連接超時秒數
    error = nullptr;
    r_config->connect_timeout_seconds = g_key_file_get_integer(keyfile, "General", "connect_timeout", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading connect_timeout: %s\n", error->message);
        goto error;
    }

    // 讀取redis連接地址
    error = nullptr;
    r_config->redis_host = g_key_file_get_string(keyfile, "General", "redis_host", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading redis_host: %s\n", error->message);
        goto error;
    }

    // 讀取redis連接端口
    error = nullptr;
    r_config->redis_port = g_key_file_get_integer(keyfile, "General", "redis_port", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading redis_port: %s\n", error->message);
        goto error;
    }

    // 讀取redis是否認證
    error = nullptr;
    r_config->auth = g_key_file_get_boolean(keyfile, "General", "redis_auth", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading redis_auth: %s\n", error->message);
        goto error;
    }

    // 如果不需要認證
    if (!r_config->auth)
    {
        return TRUE;
    }

    // 讀取redis用戶名
    error = nullptr;
    r_config->redis_username = g_key_file_get_string(keyfile, "General", "redis_username", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading redis_username: %s\n", error->message);
        goto error;
    }

    // 讀取redis密碼
    error = nullptr;
    r_config->redis_password = g_key_file_get_string(keyfile, "General", "redis_password", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading redis_password: %s\n", error->message);
        goto error;
    }
    return TRUE;

error:
    // 釋放配置
    destroy_redis_config();
    return FALSE;
}

/**
 * 釋放redis配置
 */
void destroy_redis_config()
{
    // 釋放配置結構體
    if (r_config)
    {
        if (r_config->redis_host) g_free(r_config->redis_host);
        if (r_config->redis_username) g_free(r_config->redis_username);
        if (r_config->redis_password) g_free(r_config->redis_password);
        g_free(r_config);
    }
}
