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
int  crypto_load_profiles(const char *base_dir, ApiProfile *out, int max_count);
bool crypto_save_profiles(const char *base_dir, const ApiProfile *profiles, int count);

/* 加密字符串，返回 Base64 编码的密文（需 free） */
char *crypto_encrypt(const char *plain);

/* 解密 Base64 密文，返回明文（需 free） */
char *crypto_decrypt(const char *b64_cipher);

#endif
