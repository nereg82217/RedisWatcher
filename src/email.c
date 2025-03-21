#include "email.h"

#include <curl/curl.h>

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
    e_config->smtp_url = nullptr;
    e_config->smtp_user = nullptr;
    e_config->smtp_password = nullptr;
    e_config->receiver = nullptr;

    // 讀取 SMTP 伺服器地址
    error = nullptr;
    e_config->smtp_url = g_key_file_get_string(keyfile, "Email", "smtp_url", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading smtp_url: %s\n", error->message);
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

    // 讀取 寄件人地址
    error = nullptr;
    e_config->sender = g_key_file_get_string(keyfile, "Email", "sender", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading sender: %s\n", error->message);
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
        if (e_config->smtp_user) g_free(e_config->smtp_user);
        if (e_config->smtp_password) g_free(e_config->smtp_password);
        if (e_config->sender) g_free(e_config->sender);
        if (e_config->receiver) g_free(e_config->receiver);
        g_free(e_config);
    }
}

/**
 * 傳送內容的資料來源
 * @param ptr 資料指針
 * @param size 大小
 * @param nmemb 元素大小
 * @param userp 用戶指針
 * @return 傳送的長度
 */
static size_t payload_source(char* ptr, size_t size, size_t nmemb, void* userp)
{
    // 需要發送的内容指針
    const auto payload_text = (const char**)userp;
    // 如果有内容
    if (*payload_text)
    {
        // 複製内容到指針
        const size_t len = strlen(*payload_text);
        memcpy(ptr, *payload_text, len);
        *payload_text = nullptr; // 傳完一次就結束
        return len;
    }
    return 0; // 結束傳送
}


/**
 * 發送電子郵件通知
 */
void send_email_notification()
{
    CURLcode res = CURLE_OK;
    GString* payload = g_string_new(nullptr);

    g_string_append_printf(payload, "To: %s\r\n", e_config->receiver);
    g_string_append_printf(payload, "From: %s\r\n", e_config->sender);
    g_string_append(payload, "Subject: Redis 錯誤通知\r\n");
    g_string_append(payload, "\r\n"); // 分隔 header 與 body
    g_string_append(payload, "Redis 的連接發生了問題，請檢查！\r\n");

    const char* payload_text = payload->str;


    const auto curl = curl_easy_init();
    if (curl)
    {
        struct curl_slist* recipients = nullptr;
        // SMTP 伺服器（587 是 TLS 明文起始的 port）
        curl_easy_setopt(curl, CURLOPT_URL, e_config->smtp_url);

        // Gmail 要求 STARTTLS
        if (e_config->smtp_tls)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        // 登入帳號與密碼（最好用 App Password）
        curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=LOGIN");
        curl_easy_setopt(curl, CURLOPT_USERNAME, e_config->smtp_user);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, e_config->smtp_password);

        // 寄件人
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, e_config->sender);

        // 收件人
        recipients = curl_slist_append(recipients, e_config->receiver);
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        // 傳送內容的資料來源
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &payload_text);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // 執行發送
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            g_printerr("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        else
            printf("Email sent successfully!\n");

        // 清理
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        g_string_free(payload, TRUE);
    }
}
