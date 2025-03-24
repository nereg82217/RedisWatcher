#pragma once
#include <glib.h>

/**
 * 阿里雲短信配置
 *
 * 參數
 *  - mobile: 手機號碼
 *  - endpoint: API 端點
 *  - key: API 密鑰
 *  - secret: API 密鑰
 *  - algorithm: 簽名算法
 */
typedef struct aliyun_sms_config
{
    char* mobile;
    char* endpoint;
    char* key;
    char* secret;
    char* algorithm;
} aliyun_sms_config;

typedef aliyun_sms_config* aliyun_sms_config_t;

/**
 * 讀取sms配置
 * @param keyfile 配置文件
 * @param error 錯誤對象
 */
gboolean init_sms_config(GKeyFile* keyfile, GError* error);

/**
 * 釋放sms配置
 */
void destroy_sms_config();

/**
 * 發送短信
 */
void send_sms();
