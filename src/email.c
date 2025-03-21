#include "email.h"

// email 配置
email_config_t e_config = nullptr;

/**
 * 讀取email配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_email_config(GKeyFile* keyfile, GError* error)
{
    // 創建 redis 配置對象
    e_config = g_malloc0(sizeof(email_config));
    e_config->smtp_host = nullptr;
    e_config->smtp_user = nullptr;
    e_config->smtp_password = nullptr;
    e_config->receiver = nullptr;

    // 讀取 SMTP 伺服器地址
    error = nullptr;
    e_config->smtp_host = g_key_file_get_string(keyfile, "Email", "smtp_host", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading smtp_host: %s\n", error->message);
        goto error;
    }

    // 讀取 SMTP 伺服器端口
    error = nullptr;
    e_config->smtp_port = g_key_file_get_integer(keyfile, "Email", "smtp_port", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading smtp_port: %s\n", error->message);
        goto error;
    }

    // 讀取 SMTP 是否使用 TLS
    error = nullptr;
    e_config->smtp_tls = g_key_file_get_boolean(keyfile, "Email", "smtp_tls", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading smtp_tls: %s\n", error->message);
        goto error;
    }

    // 讀取 SMTP 用戶名
    error = nullptr;
    e_config->smtp_user = g_key_file_get_string(keyfile, "Email", "smtp_user", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading smtp_user: %s\n", error->message);
        goto error;
    }

    // 讀取 SMTP 密碼
    error = nullptr;
    e_config->smtp_password = g_key_file_get_string(keyfile, "Email", "smtp_password", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading smtp_password: %s\n", error->message);
        goto error;
    }

    // 讀取 收件人地址
    error = nullptr;
    e_config->receiver = g_key_file_get_string(keyfile, "Email", "receiver", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading receiver: %s\n", error->message);
        goto error;
    }
    return TRUE;
error:
    // 釋放配置
    destroy_email_config();
    return FALSE;
}


/**
 * 釋放email配置
 */
void destroy_email_config()
{
    // 釋放配置結構體
    if (e_config)
    {
        if (e_config->smtp_host) g_free(e_config->smtp_host);
        if (e_config->smtp_user) g_free(e_config->smtp_user);
        if (e_config->smtp_password) g_free(e_config->smtp_password);
        if (e_config->receiver) g_free(e_config->receiver);
        g_free(e_config);
    }
}
