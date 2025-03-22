#pragma once

#include <glib.h>

/**
 * 讀取watcher配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_watcher_config(GKeyFile* keyfile, GError* error);

/**
 * 釋放watcher配置
 */
void destroy_watcher_config();

/**
 * 獲取服務版本
 * @param service_id 服務ID
 */
guint64 get_services_version(const gchar* service_id);

/**
 * 重啓 Docker 容器
 */
void restart_docker_container(const gchar* service_id);

/**
 * 開始事件循環
 * @return 返回值
 */
int run_loop();
