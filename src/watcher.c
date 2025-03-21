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

/**
 * 重啓 Docker 容器
 */
void restart_docker_container(const gchar* service_id)
{
    // 建立更新服務的 URL
    auto url = g_strdup_printf("http://localhost/services/%s/update?version=latest", service_id);

    // 使用 jansson 建立 JSON 主體 {"TaskTemplate": {"ForceUpdate": 1}}
    json_t* root = json_object();
    json_t* task_template = json_object();
    json_object_set(task_template, "ForceUpdate", json_integer(1));
    json_object_set(root, "TaskTemplate", task_template);

    // 將 JSON 物件轉換成字串
    char* json_data = json_dumps(root, 0);
    if (!json_data)
    {
        g_printerr("Failed to dump JSON data\n");
        json_decref(root);
        return;
    }

    // 初始化CURL
    const auto curl = curl_easy_init();
    // 如果初始化失敗就返回
    if (curl == nullptr)
    {
        g_printerr("Failed to initialize CURL\n");
        free(json_data);
        json_decref(root);
        return;
    }

    // 設定使用 Docker 的 Unix socket
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, docker_socket);
    curl_easy_setopt(curl, CURLOPT_URL, url);

    // 使用 POST 方法，並設定 JSON 主體
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

    // 設定 HTTP Header
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 執行請求
    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() 失敗: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(json_data);
        json_decref(root);
        return;
    }

    // 清理資源
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_data);
    json_decref(root);
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

    // 發送 AUTH 指令
    redisReply* reply = redisCommand(c, "AUTH %s %s", r_config->redis_username, r_config->redis_password);
    if (reply == nullptr)
    {
        printf("Sending AUTH failed, the connection may have been reset or Redis hangs\n");
        goto finish;
    }
    // 清理
    freeReplyObject(reply);

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
