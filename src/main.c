#include <glib.h>
#include <glib/gprintf.h>
#include <event2/event.h>
#include <event2/util.h>
#include <unistd.h>
#include <hiredis/hiredis.h>

#include "redis.h"
#include "email.h"

// 配置文件路徑
gchar* config_file = nullptr;

// 命令行選項
static GOptionEntry entries[] = {
    {"config_file", 'c', 0, G_OPTION_ARG_STRING, &config_file, "Configuration file path", nullptr},
    {nullptr}
};


/**
 * 重啓 Docker 容器
 */
void restart_docker_container()
{
    // TODO - 這裡應該重啓 Docker 容器
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
            redisFree(c);
        }
        else
        {
            g_printerr("Redis connection error: can't allocate redis context\n");
        }
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

/**
 * 初始化函數
 * @param argc 參數數量
 * @param argv 參數列表
 */
void init_global_params(int argc, char* argv[])
{
    // 定义错误信息
    GError* error = nullptr;

    // 创建 GOptionContext，并提供程序用途说明
    GOptionContext* context = g_option_context_new("- Redis connection health checker");

    // 添加命令行选项
    g_option_context_add_main_entries(context, entries, nullptr);

    // 设置更详细的说明
    g_option_context_set_summary(context,
                                 "RedisWatcher: A command-line tool to monitor Redis connection status.\n"
                                 "It periodically checks the availability of a Redis server and logs issues.\n"
                                 "If the connection fails, it can automatically restart the Docker container.\n"
                                 "This tool is useful for ensuring high availability and detecting failures in Redis-based applications."
    );


    // 解析命令行参数
    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        g_print("Option parsing failed: %s\n", error->message);
        if (error != nullptr) g_error_free(error);;
        exit(1);
    }

    // 釋放 GOptionContext
    g_option_context_free(context);

    // 檢查必填參數
    if (config_file == nullptr)
    {
        g_printerr("Error: --config_file is required\n");
        exit(1);
    }
}

/**
 * 解析配置文件
 */
void read_config()
{
    // 定義錯誤信息
    GError* error = nullptr;
    // 創建 KeyFile 對象
    GKeyFile* keyfile = g_key_file_new();

    // 加載 INI 文件
    if (!g_key_file_load_from_file(keyfile, config_file, G_KEY_FILE_NONE, &error))
    {
        g_printerr("Error loading config file: %s\n", error->message);
        goto error;
    }

    // 讀取 Redis 配置
    if (!init_redis_config(keyfile, error)) goto error;

    // 讀取 Email 配置
    if (!init_email_config(keyfile, error)) goto error;

    goto success;

error:
    // 釋放 redis 配置
    destroy_redis_config();
    // 釋放 email 配置
    destroy_email_config();
    // 釋放 配置文件
    if (error != nullptr) g_error_free(error);;
    g_key_file_free(keyfile);
    // 退出程序
    exit(1);

success:
    // 釋放 配置文件
    if (error != nullptr) g_error_free(error);;
    g_key_file_free(keyfile);
}


int main(int argc, char* argv[])
{
    // 初始化全局參數
    init_global_params(argc, argv);
    // 讀取配置文件
    read_config();
    // 運行事件循環
    const int res = run_loop();
    // 釋放資源
    g_free(config_file);
    // 釋放redis配置
    destroy_redis_config();
    // 釋放 email 配置
    destroy_email_config();
    return res;
}
