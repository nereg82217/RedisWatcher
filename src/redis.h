#pragma once
#include <glib.h>

/**
 * Redis 配置
 *
 * 配置:
 *  - interval_seconds 定時間隔秒數
 *  - connect_timeout_seconds 連接超時秒數
 *  - redis_host Redis 連接地址
 *  - redis_port Redis 連接端口
 *  - redis_username Redis 用戶名
 *  - redis_password Redis 密碼
 */
typedef struct redis_config
{
    // 定時間隔秒數
    gint64 interval_seconds;
    // 連接超時秒數
    gint64 connect_timeout_seconds;
    // Redis 連接地址
    gchar* redis_host;
    // Redis 連接端口
    gint redis_port;
    // Redis 用戶名
    gchar* redis_username;
    // Redis 密碼
    gchar* redis_password;
    // 是否認證
    gboolean auth;
} redis_config;

typedef redis_config* redis_config_t;

extern redis_config_t r_config;

/**
 * 讀取redis配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_redis_config(GKeyFile* keyfile, GError* error);

/**
 * 釋放redis配置
 */
void destroy_redis_config();
