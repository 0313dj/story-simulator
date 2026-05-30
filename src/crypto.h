#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdbool.h>

#define MAX_API_PROFILES 20
#define MAX_PROFILE_NAME 64

/* API 配置文件 */
typedef struct {
    char name[MAX_PROFILE_NAME];
    char endpoint[256];
    char api_key[256];
    char model[64];
} ApiProfile;

/* 多配置文件存取 */
int  crypto_load_profiles(ApiProfile *out, int max_count);
bool crypto_save_profiles(const ApiProfile *profiles, int count);

/* 加密字符串，返回 Base64 编码的密文（需 free） */
char *crypto_encrypt(const char *plain);

/* 解密 Base64 密文，返回明文（需 free） */
char *crypto_decrypt(const char *b64_cipher);

/* 加密并保存到文件 */
bool crypto_save(const char *filename, const char *endpoint,
                 const char *key, const char *model);

/* 从文件读取并解密，out_* 为 NULL 时不读取该项 */
bool crypto_load(const char *filename, char *out_endpoint, int ep_size,
                 char *out_key, int key_size, char *out_model, int md_size);

#endif
