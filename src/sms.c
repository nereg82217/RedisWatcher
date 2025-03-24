#include "sms.h"

#include <jansson.h>
#include <glib.h>
#include <curl/curl.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

// 阿里雲短信配置
aliyun_sms_config_t ali_config = nullptr;

/**
 * 讀取sms配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_sms_config(GKeyFile* keyfile, GError* error)
{
    // 申請内存
    ali_config = g_malloc0(sizeof(aliyun_sms_config));

    // 讀取電話號碼
    error = nullptr;
    ali_config->mobile = g_key_file_get_string(keyfile, "Sms", "mobile", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading mobile: %s\n", error->message);
        goto error;
    }

    // 讀取api 端點
    error = nullptr;
    ali_config->endpoint = g_key_file_get_string(keyfile, "Sms", "endpoint", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading endpoint: %s\n", error->message);
        goto error;
    }

    // 讀取api key
    error = nullptr;
    ali_config->key = g_key_file_get_string(keyfile, "Sms", "key", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading key: %s\n", error->message);
        goto error;
    }

    // 讀取api密鑰
    error = nullptr;
    ali_config->secret = g_key_file_get_string(keyfile, "Sms", "secret", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading secret: %s\n", error->message);
        goto error;
    }

    // 讀取api算法
    error = nullptr;
    ali_config->algorithm = g_key_file_get_string(keyfile, "Sms", "algorithm", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading algorithm: %s\n", error->message);
        goto error;
    }

    return TRUE;
error:
    // 釋放配置
    destroy_sms_config();
    return FALSE;
}

/**
 * 釋放sms配置
 */
void destroy_sms_config()
{
    // 如果配置為空，則直接返回
    if (ali_config == nullptr)return;
    // 釋放短信號碼
    if (ali_config->mobile != nullptr)g_free(ali_config->mobile);
    // 釋放api端點
    if (ali_config->endpoint != nullptr)g_free(ali_config->endpoint);
    // 釋放api key
    if (ali_config->key != nullptr)g_free(ali_config->key);
    // 釋放api密鑰
    if (ali_config->secret != nullptr)g_free(ali_config->secret);
    // 釋放api算法
    if (ali_config->algorithm != nullptr)g_free(ali_config->algorithm);
    // 釋放配置
    g_free(ali_config);
}


/**
 * 生成nonce隨機數
 * @return 隨機數 (需要手動釋放)
 */
gchar* generate_uuid()
{
    // 分配固定大小的記憶體（36字節 + null 結尾）
    gchar* uuid = g_malloc0(37); // 自動補 '\0'

    // 產生16個隨機位元組
    guchar random_bytes[16];
    if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1)
    {
        g_printerr("RAND_bytes() failed\n");
        g_free(uuid);
        return nullptr;
    }

    // 設定版本與變體
    random_bytes[6] = (random_bytes[6] & 0x0F) | 0x40; // Version 4
    random_bytes[8] = (random_bytes[8] & 0x3F) | 0x80; // Variant

    // 格式化為 UUID 字串
    g_snprintf(
        uuid,
        37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
        random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7],
        random_bytes[8], random_bytes[9], random_bytes[10], random_bytes[11],
        random_bytes[12], random_bytes[13], random_bytes[14], random_bytes[15]);

    return uuid; // 使用者記得 g_free
}

/**
 * 計算 SHA-256 哈希
 * @param input 輸入
 * @return 輸出 (需要手動釋放)
 */
gchar* sha256_hex(const gchar* input)
{
    // 定義 SHA-256 容器
    guchar hash[SHA256_DIGEST_LENGTH];

    // 計算 SHA-256
    SHA256((const guchar*)input, strlen(input), hash);

    // 分配輸出字串記憶體：64位（2字元/byte）+ 結尾符
    gchar* output = g_malloc0(SHA256_DIGEST_LENGTH * 2 + 1);

    for (gint i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        g_snprintf(output + (i * 2), 3, "%02x", hash[i]);
    }

    return output;
}

/**
 * 構建規範化標頭
 * @param host 主機地址
 * @param x_acs_action API名稱
 * @param hashed_payload 哈希請求體
 * @param x_acs_date 請求時間
 * @param uuid 隨機數
 * @param x_acs_version API版本
 * @return 規範化標頭 (需要手動釋放)
 */
gchar* build_canonical_headers(
    const gchar* host,
    const gchar* x_acs_action,
    const gchar* hashed_payload,
    const gchar* x_acs_date,
    const gchar* uuid,
    const gchar* x_acs_version
)
{
    GString* canonical = g_string_new(nullptr);

    g_string_append_printf(canonical, "host:%s\n", host);
    g_string_append_printf(canonical, "x-acs-action:%s\n", x_acs_action);
    g_string_append_printf(canonical, "x-acs-content-sha256:%s\n", hashed_payload);
    g_string_append_printf(canonical, "x-acs-date:%s\n", x_acs_date);
    g_string_append_printf(canonical, "x-acs-signature-nonce:%s\n", uuid);
    g_string_append_printf(canonical, "x-acs-version:%s", x_acs_version); // 最後一行不用換行

    gchar* result = g_string_free(canonical, FALSE); // 轉為 gchar*，記得 g_free
    return result;
}

/**
 * 構建規範化請求
 * @param http_method 請求方法
 * @param canonical_uri 請求URI
 * @param query_params 查詢參數
 * @param canonical_headers 規範化標頭
 * @param signed_headers 簽名欄位
 * @param hashed_payload 哈希請求體
 * @return 規範化請求 (需要手動釋放)
 */
gchar* build_canonical_request(
    const gchar* http_method,
    const gchar* canonical_uri,
    const gchar* query_params,
    const gchar* canonical_headers,
    const gchar* signed_headers,
    const gchar* hashed_payload
)
{
    GString* request = g_string_new(nullptr);

    g_string_append_printf(request, "%s\n", http_method);
    g_string_append_printf(request, "%s\n", canonical_uri);
    g_string_append_printf(request, "%s\n", query_params ? query_params : "");
    g_string_append_printf(request, "%s\n\n", canonical_headers);
    g_string_append_printf(request, "%s\n", signed_headers);
    g_string_append(request, hashed_payload); // 最後一行不用換行

    return g_string_free(request, FALSE);
}

/**
 * 構建待簽名字串
 * @param algorithm 簽名算法
 * @param hashed_canonical_request 正常化請求的SHA-256
 * @return 待簽名字串 (需要手動釋放)
 */
gchar* build_string_to_sign(
    const gchar* algorithm,
    const gchar* hashed_canonical_request
)
{
    GString* str = g_string_new(nullptr);
    g_string_append_printf(str, "%s\n%s", algorithm, hashed_canonical_request);
    return g_string_free(str, FALSE);
}

/**
 * HMAC-SHA256 簽名
 * @param key 密鑰
 * @param message 訊息
 * @return 簽名 (需要手動釋放)
 */
gchar* hmac256(const gchar* key, const gchar* message)
{
    guchar hmac[SHA256_DIGEST_LENGTH];
    unsigned int len = 0;

    // 計算 HMAC
    HMAC(EVP_sha256(),
         key, (int)strlen(key),
         (const guchar*)message, strlen(message),
         hmac, &len);

    // 分配 hex 字串空間（64字元 + null 結尾）
    gchar* output = g_malloc0(SHA256_DIGEST_LENGTH * 2 + 1);

    for (gint i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        g_snprintf(output + (i * 2), 3, "%02x", hmac[i]);
    }

    return output;
}

/**
 * 構建授權頭
 * @param algorithm 簽名算法
 * @param access_key_id Access Key ID
 * @param signed_headers 簽名欄位
 * @param signature 簽名
 * @return 授權頭 (需要手動釋放)
 */
gchar* build_authorization_header(
    const gchar* algorithm,
    const gchar* access_key_id,
    const gchar* signed_headers,
    const gchar* signature
)
{
    GString* auth = g_string_new(nullptr);

    g_string_append_printf(
        auth,
        "%s Credential=%s,SignedHeaders=%s,Signature=%s",
        algorithm,
        access_key_id,
        signed_headers,
        signature
    );

    return g_string_free(auth, FALSE);
}

/**
 * 獲取authorization請求頭
 * @param http_method 請求方法
 * @param canonical_uri 請求URI
 * @param host 主機地址
 * @param x_acs_action API名稱
 * @param x_acs_version API版本
 * @param query_params 查詢參數
 * @param body 請求體
 * @param authorization_header 授權頭
 * @param hashed_payload 哈希請求體
 * @param x_acs_date 請求時間
 * @param uuid 隨機數
 */
void get_authorization(
    const gchar* http_method,
    const gchar* canonical_uri,
    const gchar* host,
    const gchar* x_acs_action,
    const gchar* x_acs_version,
    const gchar* x_acs_date,
    const gchar* query_params,
    const gchar* body,
    gchar** authorization_header,
    gchar** hashed_payload,
    gchar** uuid
)
{
    // 產生UUID
    *uuid = generate_uuid();

    // 計算請求體的 SHA-256 哈希 (即 x-acs-content-sha256)
    *hashed_payload = sha256_hex(body ? body : "");

    // 構建規範化標頭
    const auto canonical_headers = build_canonical_headers(
        host,
        x_acs_action,
        *hashed_payload,
        x_acs_date,
        *uuid,
        x_acs_version
    );

    // 簽名欄位
    const auto signed_headers = "host;x-acs-action;x-acs-content-sha256;x-acs-date;x-acs-signature-nonce;x-acs-version";

    // 構建規範化請求
    const auto canonical_request = build_canonical_request(
        http_method,
        canonical_uri,
        query_params,
        canonical_headers,
        signed_headers,
        *hashed_payload
    );

    // 計算正常化請求的SHA-256雜湊
    const auto hashed_canonical_request = sha256_hex(canonical_request);

    // 構建待簽名字串
    const auto string_to_sign = build_string_to_sign(ali_config->algorithm, hashed_canonical_request);

    // 計算簽名
    const auto signature = hmac256(ali_config->secret, string_to_sign);


    // 構建最終的 Authorization 頭，包含 SignedHeaders
    *authorization_header = build_authorization_header(ali_config->algorithm, ali_config->key, signed_headers,
                                                       signature);

    // 釋放資源
    g_free(canonical_headers);
    g_free(canonical_request);
    g_free(hashed_canonical_request);
    g_free(string_to_sign);
    g_free(signature);
}

/**
 * 構建請求URL
 * @param host 主機地址
 * @param canonical_uri 請求URI
 * @param query_params 查詢參數
 * @return 請求URL (需要手動釋放)
 */
gchar* build_request_url(
    const gchar* host,
    const gchar* canonical_uri,
    const gchar* query_params
)
{
    GString* url = g_string_new(nullptr);

    if (query_params && *query_params != '\0')
    {
        g_string_append_printf(url, "https://%s%s?%s", host, canonical_uri, query_params);
    }
    else
    {
        g_string_append_printf(url, "https://%s%s", host, canonical_uri);
    }

    return g_string_free(url, FALSE);
}

/**
 * 構建cURL標頭
 * @param content_type 請求類型
 * @param authorization_header 授權頭
 * @param host 主機地址
 * @param x_acs_action API名稱
 * @param hashed_payload 哈希請求體
 * @param x_acs_date 請求時間
 * @param uuid 隨機數
 * @param x_acs_version API版本
 * @return cURL標頭 (需要手動釋放)
 */
struct curl_slist* build_curl_headers(
    const gchar* content_type,
    const gchar* authorization_header,
    const gchar* host,
    const gchar* x_acs_action,
    const gchar* hashed_payload,
    const gchar* x_acs_date,
    const gchar* uuid,
    const gchar* x_acs_version
)
{
    struct curl_slist* headers = nullptr;
    GString* header = g_string_new(nullptr);

    // Content-Type
    g_string_printf(header, "Content-Type: %s", content_type);
    headers = curl_slist_append(headers, header->str);

    // Authorization
    g_string_printf(header, "Authorization: %s", authorization_header);
    headers = curl_slist_append(headers, header->str);

    // Host
    g_string_printf(header, "host: %s", host);
    headers = curl_slist_append(headers, header->str);

    // x-acs-action
    g_string_printf(header, "x-acs-action: %s", x_acs_action);
    headers = curl_slist_append(headers, header->str);

    // x-acs-content-sha256
    g_string_printf(header, "x-acs-content-sha256: %s", hashed_payload);
    headers = curl_slist_append(headers, header->str);

    // x-acs-date
    g_string_printf(header, "x-acs-date: %s", x_acs_date);
    headers = curl_slist_append(headers, header->str);

    // x-acs-signature-nonce
    g_string_printf(header, "x-acs-signature-nonce: %s", uuid);
    headers = curl_slist_append(headers, header->str);

    // x-acs-version
    g_string_printf(header, "x-acs-version: %s", x_acs_version);
    headers = curl_slist_append(headers, header->str);

    g_string_free(header, TRUE); // 清理 GString
    return headers; // 呼叫者 curl_slist_free_all()
}

/**
 * 調用API
 * @param http_method 請求方法
 * @param canonical_uri 請求URI
 * @param host 主機地址
 * @param x_acs_action API名稱
 * @param x_acs_version API版本
 * @param query_params 查詢參數
 * @param body 請求體
 * @param content_type 請求類型
 * @param body_length 請求體長度
 */
void call_api(
    const gchar* http_method,
    const gchar* canonical_uri,
    const gchar* host,
    const gchar* x_acs_action,
    const gchar* x_acs_version,
    const gchar* query_params,
    const gchar* body,
    const gchar* content_type,
    size_t body_length
)
{
    // 擷取簽名所需的參數值
    gchar* authorization_header = nullptr;
    gchar* hashed_payload = nullptr;
    gchar* uuid = nullptr;

    // 產生x-acs-date
    const auto now = g_date_time_new_now_utc();
    const auto x_acs_date = g_date_time_format(now, "%Y-%m-%dT%H:%M:%SZ");
    g_date_time_unref(now);

    // 擷取授權頭
    get_authorization(
        http_method,
        canonical_uri,
        host,
        x_acs_action,
        x_acs_version,
        x_acs_date,
        query_params,
        body,
        &authorization_header,
        &hashed_payload,
        &uuid
    );

    // 構建請求路徑 URL
    const auto url = build_request_url(host, canonical_uri, query_params);

    // 初始化cURL
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        g_printerr("curl_easy_init() failed\n");
        return;
    }

    // 定義數組用於添加要求標頭
    struct curl_slist* headers = build_curl_headers(
        content_type,
        authorization_header,
        host,
        x_acs_action,
        hashed_payload,
        x_acs_date,
        uuid,
        x_acs_version
    );

    // 設定cURL選項
    // 設定cURL要求方法
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, http_method);
    // 設定 url
    curl_easy_setopt(curl, CURLOPT_URL, url);
    // 禁用 SSL 驗證，（調試）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // 添加調試資訊
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    // 添加要求標頭
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 添加 body 請求體
    if (body)
    {
        // 佈建要求體的大小
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body_length);

        if (strcmp(content_type, "application/octet-stream") == 0)
        {
            // 添加請求體
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
        else if (strcmp(content_type, "application/x-www-form-urlencoded") == 0)
        {
            // 添加請求體
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
        else if (strcmp(content_type, "application/json; charset=utf-8") == 0)
        {
            // 添加請求體
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    }

    // 執行請求並檢查響應
    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        g_printerr("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return;
    }

    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);


    // 釋放資源
    g_free(authorization_header);
    g_free(hashed_payload);
    g_free(uuid);
    g_free(x_acs_date);

    g_free(url);
}

/**
 * 發送短信
 */
void send_sms()
{
    const gchar* http_method = "POST";
    const gchar* canonical_uri = "/";
    const gchar* x_acs_action = "SendMessageToGlobe";
    const gchar* x_acs_version = "2018-05-01";
    const gchar* content_type = "application/x-www-form-urlencoded";

    // 定義查詢參數
    const auto query_params = "";

    // 構建JSON格式的請求體
    const auto body = g_string_new(nullptr);
    g_string_append_printf(body, "To=%s&", ali_config->mobile);
    g_string_append_printf(body, "Message=%s", "Redis 的連接發生了問題，請檢查！");

    // 發送請求
    call_api(
        http_method,
        canonical_uri,
        ali_config->endpoint,
        x_acs_action,
        x_acs_version,
        query_params,
        body->str,
        content_type,
        body->len
    );

    g_string_free(body, TRUE);
}
