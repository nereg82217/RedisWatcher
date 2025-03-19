#include <glib.h>
#include <glib/gprintf.h>
#include <event2/event.h>
#include <event2/util.h>
#include <unistd.h>
#include <hiredis/hiredis.h>

// 定時間隔秒數
gint64 interval_seconds = 60;
// 連接超時秒數
gint64 connect_timeout_seconds = 5;
// Redis 連接地址
gchar* redis_host = nullptr;
// Redis 連接端口
gint redis_port = 0;

static GOptionEntry entries[] = {
    {"redis-host", 'h', 0, G_OPTION_ARG_STRING, &redis_host, "Redis host (required)", "HOST"},
    {"redis-port", 'p', 0, G_OPTION_ARG_INT, &redis_port, "Redis port (default: 6379)", "PORT"},
    {"interval", 'i', 0, G_OPTION_ARG_INT, &interval_seconds, "Interval in seconds (default: 60)", "SECONDS"},
    {
        "timeout", 'o', 0, G_OPTION_ARG_INT, &connect_timeout_seconds, "Connect timeout in seconds (default: 5)",
        "SECONDS"
    },
    {nullptr}
};

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
    const struct timeval timeout = {connect_timeout_seconds, 0};
    redisContext* c = redisConnectWithTimeout(redis_host, redis_port, timeout);

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

    g_printf("Redis connection success\n");
    // 釋放 Redis 連接
    redisFree(c);

    const auto ev = (struct event*)arg;
    const struct timeval interval = {interval_seconds, 0};
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
    const struct timeval interval = {interval_seconds, 0};

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
    g_print("Timer started with interval %ld seconds.\n", interval_seconds);

    // 运行事件循环
    event_base_dispatch(base);

    // 释放资源
    event_free(timer_event);
    event_base_free(base);

    return 0;
}

// **初始化函數**
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
        g_error_free(error);
        exit(1);
    }

    // 釋放 GOptionContext
    g_option_context_free(context);

    // **檢查必填參數**
    if (redis_host == nullptr)
    {
        g_printerr("Error: --redis-host is required\n");
        exit(1);
    }
}

int main(int argc, char* argv[])
{
    // 初始化全局參數
    init_global_params(argc, argv);
    // 運行事件循環
    const int res = run_loop();
    // 釋放資源
    g_free(redis_host);
    return res;
}
