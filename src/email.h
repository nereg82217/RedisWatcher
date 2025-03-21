#pragma once
#include <glib.h>

/**
 * 郵件配置
 *
 * 配置:
 *  - smtp_url SMTP 伺服器地址
 *  - smtp_tls 是否使用 TLS
 *  - smtp_user SMTP 用戶名
 *  - smtp_password SMTP 密碼
 *  - sender 寄件人地址
 *  - receiver 收件人地址
 */
typedef struct email_config
{
    gchar* smtp_url;
    gboolean smtp_tls;
    gchar* smtp_user;
    gchar* smtp_password;
    gchar* sender;
    gchar* receiver;
} email_config;

typedef email_config* email_config_t;

extern email_config_t e_config;

/**
 * 讀取email配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_email_config(GKeyFile* keyfile, GError* error);

/**
 * 釋放email配置
 */
void destroy_email_config();

/**
 * 發送電子郵件通知
 */
void send_email_notification();
