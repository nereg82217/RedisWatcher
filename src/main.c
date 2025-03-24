#include <glib.h>

#include "redis.h"
#include "email.h"
#include "watcher.h"
#include "sms.h"

// 配置文件路徑
gchar* config_file = nullptr;

// 命令行選項
static GOptionEntry entries[] = {
    {"config_file", 'c', 0, G_OPTION_ARG_STRING, &config_file, "Configuration file path", nullptr},
    {nullptr}
};


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

    // 讀取 Watcher 配置
    if (!init_watcher_config(keyfile, error)) goto error;

    // 讀取 Sms 配置
    if (!init_sms_config(keyfile, error)) goto error;

    goto success;

error:
    // 釋放 redis 配置
    destroy_redis_config();
    // 釋放 email 配置
    destroy_email_config();
    // 釋放 watcher 配置
    destroy_watcher_config();
    // 釋放 sms 配置
    destroy_sms_config();
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
    // 釋放 watcher 配置
    destroy_watcher_config();
    // 釋放 sms 配置
    destroy_sms_config();
    return res;
}
