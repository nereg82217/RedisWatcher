#include "watcher.h"

#include <glib.h>
#include <glib/gprintf.h>
#include <event2/event.h>
#include <event2/util.h>
#include <unistd.h>
#include <hiredis/hiredis.h>
#include <curl/curl.h>
#include <jansson.h>

#include "email.h"
#include "redis.h"
#include "sms.h"

// 服務列表數量
gsize n_services = 0;
// 服務列表設置
gchar** services = nullptr;
// Docker Unix socket
gchar* docker_socket = nullptr;

// 錯誤是否在持續中
gboolean error_ongoing = FALSE;

/**
 * 讀取watcher配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_watcher_config(GKeyFile* keyfile, GError* error)
{
    // 讀取服務列表
    error = nullptr;
    services = g_key_file_get_string_list(keyfile, "Services", "targets", &n_services, &error);
    if (error != nullptr)
    {
        g_printerr("Error reading targets: %s\n", error->message);
        goto error;
    }
    // 讀取docker socket
    error = nullptr;
    docker_socket = g_key_file_get_string(keyfile, "Services", "socket", &error);
    if (error != nullptr)
    {
        g_printerr("Error reading socket: %s\n", error->message);
        goto error;
    }
    return TRUE;

error:
    // 釋放配置
    destroy_watcher_config();
    return FALSE;
}

/**
 * 釋放watcher配置
 */
void destroy_watcher_config()
{
    if (services != nullptr)
    {
        // 釋放服務列表
        g_strfreev(services);
        // 釋放docker socket
        g_free(docker_socket);
    }
}

// 寫入 callback：將回傳的資料塞入 string
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t realsize = size * nmemb;
    const auto mem = (GString*)userp;
    g_string_append_len(mem, (const gchar*)contents, realsize);
    return realsize;
}

/**
 * 獲取服務版本
 * @param service_id 服務ID
 */
guint64 get_services_version(const gchar* service_id)
{
    // 服務版本
    guint64 index = 0;

    // 建立更新服務的 URL
    auto url = g_strdup_printf("http://localhost/services/%s", service_id);

    // 初始化CURL
    const auto curl = curl_easy_init();
    // 如果初始化失敗就返回
    if (curl == nullptr)
    {
        g_printerr("Failed to initialize CURL\n");
        g_free(url);
        return index;
    }

    // 定義響應數據
    GString* response = g_string_new("");

    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, docker_socket);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

    // 執行請求
    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() 失敗: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        g_string_free(response, TRUE);
        g_free(url);
        return index;
    }

    // 使用 jansson 解析 JSON
    json_error_t error;
    json_t* root = json_loads(response->str, 0, &error);
    if (!root)
    {
        g_printerr("JSON parse error: on line %d: %s\n", error.line, error.text);
    }
    else
    {
        const json_t* version_obj = json_object_get(root, "Version");
        if (version_obj && json_is_object(version_obj))
        {
            json_t* index_obj = json_object_get(version_obj, "Index");
            if (index_obj && json_is_integer(index_obj))
            {
                index = json_integer_value(index_obj);
            }
            else
            {
                g_printerr("Index not found or not an integer\n");
            }
        }
        else
        {
            g_printerr("Version object not found or not valid\n");
        }

        json_decref(root); // Free JSON root object
    }

    // 清理資源
    curl_easy_cleanup(curl);
    g_free(url);

    return index;
}

/**
 * 重啓 Docker 容器
 */
void restart_docker_container(const gchar* service_id)
{
    // 初始化CURL
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        g_printerr("Failed to initialize CURL\n");
        return;
    }

    // 獲取服務詳情的 URL
    auto url = g_strdup_printf("http://localhost/services/%s", service_id);

    // 定義響應數據
    GString* response = g_string_new("");

    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, docker_socket);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

    // 執行請求
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        g_printerr("GET failed: %s\n", curl_easy_strerror(res));
        goto cleanup;
    }

    // Step 2: 解析 JSON
    json_error_t error;
    json_t* root = json_loads(response->str, 0, &error);
    if (!root)
    {
        g_printerr("JSON parse error: %s (line %d)\n", error.text, error.line);
        goto cleanup;
    }

    // 取出 version
    const json_t* version_obj = json_object_get(root, "Version");
    const json_t* index_obj = json_object_get(version_obj, "Index");
    if (!json_is_integer(index_obj))
    {
        g_printerr("Invalid version index\n");
        json_decref(root);
        goto cleanup;
    }
    guint64 version_index = json_integer_value(index_obj);

    // 取出 Spec
    json_t* spec_obj = json_object_get(root, "Spec");
    if (!json_is_object(spec_obj))
    {
        g_printerr("Missing Spec object\n");
        json_decref(root);
        goto cleanup;
    }

    // 複製 Spec 出來（防止改到原本的 root）
    json_t* spec_copy = json_deep_copy(spec_obj);

    // 加 ForceUpdate += 1
    json_t* task_template = json_object_get(spec_copy, "TaskTemplate");
    if (!task_template || !json_is_object(task_template))
    {
        g_printerr("Missing TaskTemplate\n");
        json_decref(spec_copy);
        json_decref(root);
        goto cleanup;
    }

    const json_t* force_update_obj = json_object_get(task_template, "ForceUpdate");
    json_int_t force_update = 0;
    if (force_update_obj && json_is_integer(force_update_obj))
    {
        force_update = json_integer_value(force_update_obj);
    }
    json_object_set(task_template, "ForceUpdate", json_integer(force_update + 1));

    // Step 3: 發送 POST /services/<id>/update?version=<version_index>
    // 建立更新服務的 URL
    g_free(url);
    url = g_strdup_printf("http://localhost/services/%s/update?version=%lu", service_id, version_index);

    // 將 JSON 轉換為字符串
    char* json_payload = json_dumps(spec_copy, JSON_COMPACT);

    // 重置CURL
    curl_easy_reset(curl);
    // 設定使用 Docker 的 Unix socket
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, docker_socket);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    // 使用 POST 方法，並設定 JSON 主體
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    // 設定 HTTP Header
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 執行請求
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        g_printerr("Service update failed: %s\n", curl_easy_strerror(res));
    }
    else
    {
        g_print("Service '%s' restarted successfully.\n", service_id);
    }

    // 清理
    curl_slist_free_all(headers);
    free(json_payload);
    json_decref(spec_copy);
    json_decref(root);

cleanup:
    curl_easy_cleanup(curl);
    g_string_free(response, TRUE);
    g_free(url);
}

/**
 * 定时器回调函数
 * @param fd 文件描述符
 * @param event 事件类型
 * @param arg 回调函数参数
 */
void timer_callback(const evutil_socket_t fd, const short event, void* arg)
{
    (void)fd; // 未使用
    (void)event; // 未使用
    g_print("Timer callback called.\n");

    // 建立 Redis 連接
    const struct timeval timeout = {r_config->connect_timeout_seconds, 0};
    const auto c = redisConnectWithTimeout(r_config->redis_host, r_config->redis_port, timeout);

    // 如果連接失敗，則輸出錯誤信息
    if (c == nullptr || c->err)
    {
        if (c)
        {
            g_printerr("Redis connection error: %s\n", c->errstr);
            // 如果先前未發生錯誤
            if (!error_ongoing)
            {
                // 發送電子郵件通知
                send_email_notification();
                // 發送短信通知
                send_sms();
                error_ongoing = TRUE;
            }
        }
        else
        {
            g_printerr("Redis connection error: can't allocate redis context\n");
        }
        goto finish;
    }

    // 如果先前有錯誤，則重置
    if (error_ongoing)
    {
        // 重啓 Docker 容器
        for (gint i = 0; i < n_services; ++i)
        {
            restart_docker_container(services[i]);
        }
        error_ongoing = FALSE;
    }

    redisReply* reply = nullptr;

    // 如果需要驗證，發送 AUTH 指令
    if (r_config->auth)
    {
        reply = redisCommand(c, "AUTH %s %s", r_config->redis_username, r_config->redis_password);
        if (reply == nullptr)
        {
            printf("Sending AUTH failed, the connection may have been reset or Redis hangs\n");
            goto finish;
        }
        // 清理
        freeReplyObject(reply);
        reply = nullptr;
    }


    // 發送 PING 指令
    reply = redisCommand(c, "PING");
    if (reply == nullptr)
    {
        printf("Sending PING failed, the connection may have been reset or Redis hangs\n");
        goto finish;
    }

    // 檢查回應
    if (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "PONG") == 0)
    {
        printf("Redis Respond to PING: %s\n", reply->str);
    }
    else
    {
        printf("Redis responds to exceptions: type=%d, str=%s\n", reply->type, reply->str);
    }

    // 清理
    freeReplyObject(reply);
    reply = nullptr;

    g_printf("Redis connection success\n");

finish:

    if (reply)
    {
        freeReplyObject(reply);
    }
    if (c)
    {
        redisFree(c);
    }

    const auto ev = (struct event*)arg;
    const struct timeval interval = {r_config->interval_seconds, 0};
    evtimer_add(ev, &interval);
}

/**
 * 開始事件循環
 * @return 返回值
 */
int run_loop()
{
    // 创建 libevent 基础结构
    struct event_base* base = event_base_new();
    if (!base)
    {
        g_printerr("Cannot create event base!\n");
        return 1;
    }

    // 定义定时器事件
    const struct timeval interval = {r_config->interval_seconds, 0};

    // 創建定時器事件
    struct event* timer_event = evtimer_new(base, timer_callback, event_self_cbarg());
    if (!timer_event)
    {
        g_printerr("Cannot create timer event!\n");
        event_base_free(base);
        return 1;
    }

    // 启动定时器
    evtimer_add(timer_event, &interval);
    g_print("Timer started with interval %ld seconds.\n", r_config->interval_seconds);

    // 运行事件循环
    event_base_dispatch(base);

    // 释放资源
    event_free(timer_event);
    event_base_free(base);

    return 0;
}
