#include "crypto.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "crypt32.lib")

/* ── Base64 编解码 ── */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const BYTE *data, int len)
{
    int out_len = ((len + 2) / 3) * 4 + 1;
    char *out = (char *)malloc(out_len);
    if (!out) return NULL;
    int j = 0;
    for (int i = 0; i < len; i += 3) {
        int n = (data[i] << 16) | ((i + 1 < len ? data[i + 1] : 0) << 8) | (i + 2 < len ? data[i + 2] : 0);
        out[j++] = b64_table[(n >> 18) & 63];
        out[j++] = b64_table[(n >> 12) & 63];
        out[j++] = (i + 1 < len) ? b64_table[(n >> 6) & 63] : '=';
        out[j++] = (i + 2 < len) ? b64_table[n & 63] : '=';
    }
    out[j] = '\0';
    return out;
}

static BYTE *base64_decode(const char *in, int *out_len)
{
    /* Bug #20: strip whitespace (newlines, spaces) before decoding.
       This handles base64 text that may have been wrapped or indented. */
    int raw_len = (int)strlen(in);
    char *clean = (char *)malloc(raw_len + 1);
    if (!clean) return NULL;
    int ci = 0;
    for (int i = 0; i < raw_len; i++) {
        char ch = in[i];
        if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t')
            clean[ci++] = ch;
    }
    clean[ci] = '\0';

    int len = ci;
    if (len % 4 != 0) { free(clean); return NULL; }
    *out_len = len / 4 * 3;
    if (clean[len - 1] == '=') (*out_len)--;
    if (clean[len - 2] == '=') (*out_len)--;
    BYTE *out = (BYTE *)malloc(*out_len);
    if (!out) { free(clean); return NULL; }
    int j = 0;
    for (int i = 0; i < len; i += 4) {
        /* Bug #18: validate characters are in b64_table before pointer subtraction */
        const char *pa = strchr(b64_table, clean[i]);
        const char *pb = strchr(b64_table, clean[i+1]);
        if (!pa || !pb) { free(out); free(clean); return NULL; }
        int a = (int)(pa - b64_table);
        int b = (int)(pb - b64_table);
        int c = 0, d = 0;
        if (clean[i+2] != '=') {
            const char *pc = strchr(b64_table, clean[i+2]);
            if (!pc) { free(out); free(clean); return NULL; }
            c = (int)(pc - b64_table);
        }
        if (clean[i+3] != '=') {
            const char *pd = strchr(b64_table, clean[i+3]);
            if (!pd) { free(out); free(clean); return NULL; }
            d = (int)(pd - b64_table);
        }
        int n = (a << 18) | (b << 12) | (c << 6) | d;
        if (j < *out_len) out[j++] = (BYTE)((n >> 16) & 255);
        if (j < *out_len) out[j++] = (BYTE)((n >> 8) & 255);
        if (j < *out_len) out[j++] = (BYTE)(n & 255);
    }
    free(clean);
    return out;
}

/* ── DPAPI 加密 ── */
char *crypto_encrypt(const char *plain)
{
    DATA_BLOB in, out;
    in.pbData = (BYTE *)plain;
    in.cbData = (DWORD)strlen(plain) + 1;

    if (!CryptProtectData(&in, L"SimulatorAPIKey", NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return NULL;

    char *b64 = base64_encode(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return b64;
}

/* ── DPAPI 解密 ── */
char *crypto_decrypt(const char *b64_cipher)
{
    int len;
    BYTE *raw = base64_decode(b64_cipher, &len);
    if (!raw) return NULL;

    DATA_BLOB in, out;
    in.pbData = raw;
    in.cbData = len;

    char *result = NULL;
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL,
                           CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        result = (char *)malloc(out.cbData);
        if (result) memcpy(result, out.pbData, out.cbData);
        LocalFree(out.pbData);
    }
    free(raw);
    return result;
}

/* ── 多配置文件存取（V2格式）── */

int crypto_load_profiles(const char *base_dir, ApiProfile *out, int max_count)
{
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    LOG_I("crypto_load: CWD=%s", cwd);
    LOG_I("crypto_load: base_dir=%s", base_dir);

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\secrets.dat", base_dir);
    LOG_I("crypto_load: opening %s", path);

    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 65536) { fclose(fp); return 0; }

    char *raw = (char *)malloc(fsize + 1);
    if (!raw) { fclose(fp); return 0; }
    fread(raw, 1, fsize, fp);
    raw[fsize] = '\0';
    fclose(fp);

    /* V2 多配置格式 */
    if (strncmp(raw, "V2\n", 3) == 0) {
        char *b64 = raw + 3;
        /* 去掉尾部换行符 */
        int blen = (int)strlen(b64);
        while (blen > 0 && (b64[blen-1] == '\n' || b64[blen-1] == '\r'))
            b64[--blen] = '\0';
        char *ep = crypto_decrypt(b64);
        if (!ep) { free(raw); return 0; }

        char *p = ep;
        int count = atoi(p);
        LOG_I("DECRYPTED: %d profile(s)", count);
        if (count <= 0 || count > max_count) { free(ep); free(raw); return 0; }
        p = strchr(p, '\n');
        if (!p) { free(ep); free(raw); return 0; }
        p++;

        for (int i = 0; i < count; i++) {
            char *lines[4];
            int got = 0;
            for (int j = 0; j < 4; j++) {
                lines[j] = p;
                char *nl = strchr(p, '\n');
                if (!nl) break;
                *nl = '\0';
                p = nl + 1;
                got++;
            }
            if (got < 4) break;
            /* Bug #20: use field sizes from ApiProfile struct */
            strncpy(out[i].name,     lines[0], MAX_PROFILE_NAME - 1);
            out[i].name[MAX_PROFILE_NAME - 1] = '\0';
            strncpy(out[i].endpoint, lines[1], sizeof(out[i].endpoint) - 1);
            out[i].endpoint[sizeof(out[i].endpoint) - 1] = '\0';
            strncpy(out[i].api_key,  lines[2], sizeof(out[i].api_key) - 1);
            out[i].api_key[sizeof(out[i].api_key) - 1] = '\0';
            strncpy(out[i].model,    lines[3], sizeof(out[i].model) - 1);
            out[i].model[sizeof(out[i].model) - 1] = '\0';
        }
        free(ep); free(raw);
        return count;
    }

    /* 回退：V1 旧格式 → 升级至 Default */
    /* Bug #21: add logging for V1 format failures so users can diagnose issues */
    char *lines[3] = {raw, NULL, NULL};
    lines[1] = strchr(raw, '\n');
    if (!lines[1]) { LOG_W("crypto: V1 format parse failed (missing line 2)"); free(raw); return 0; }
    *lines[1] = '\0'; lines[1]++;
    lines[2] = strchr(lines[1], '\n');
    if (!lines[2]) { LOG_W("crypto: V1 format parse failed (missing line 3)"); free(raw); return 0; }
    *lines[2] = '\0'; lines[2]++;
    char *end = strchr(lines[2], '\n');
    if (end) *end = '\0';

    char *ep  = crypto_decrypt(lines[0]);
    char *key = crypto_decrypt(lines[1]);
    char *md  = crypto_decrypt(lines[2]);
    int ret = 0;
    if (ep && key && md) {
        safe_strcpy(out[0].name, "Default", MAX_PROFILE_NAME);
        safe_strcpy(out[0].endpoint, ep,  sizeof(out[0].endpoint));
        safe_strcpy(out[0].api_key,  key, sizeof(out[0].api_key));
        safe_strcpy(out[0].model,    md,  sizeof(out[0].model));
        ret = 1;
    } else {
        if (!ep)  LOG_W("crypto: V1 decrypt failed for endpoint field");
        if (!key) LOG_W("crypto: V1 decrypt failed for api_key field");
        if (!md)  LOG_W("crypto: V1 decrypt failed for model field");
        LOG_I("crypto: V1 format decryption failed — profiles file may be corrupt; "
                 "delete secrets.dat to start fresh");
    }
    free(ep); free(key); free(md); free(raw);
    return ret;
}

bool crypto_save_profiles(const char *base_dir, const ApiProfile *profiles, int count)
{
    if (count < 0 || count > MAX_API_PROFILES) return false;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\secrets.dat", base_dir);

    /* Allow count==0 to clear all profiles */
    if (count == 0) {
        char *ep = crypto_encrypt("0\n");
        if (!ep) return false;
        FILE *fp = fopen(path, "w");
        if (!fp) { free(ep); return false; }
        fprintf(fp, "V2\n%s", ep);
        fclose(fp);
        free(ep);
        return true;
    }

    int cap = 65536;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;

    int pos = snprintf(buf, cap, "%d\n", count);
    if (pos < 0 || pos >= cap) { free(buf); return false; }
    for (int i = 0; i < count; i++) {
        int wrote = snprintf(buf + pos, cap - pos, "%s\n%s\n%s\n%s\n",
            profiles[i].name, profiles[i].endpoint,
            profiles[i].api_key, profiles[i].model);
        if (wrote < 0 || wrote >= cap - pos) { free(buf); return false; }
        pos += wrote;
    }

    char *ep = crypto_encrypt(buf);
    if (!ep) { free(buf); return false; }

    /* 写入 "V2\n" + 加密正文 */
    FILE *fp = fopen(path, "w");
    if (!fp) { free(ep); free(buf); return false; }
    fprintf(fp, "V2\n%s", ep);
    fclose(fp);
    free(ep); free(buf);
    return true;
}
