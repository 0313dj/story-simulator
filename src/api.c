#include "api.h"
#include "narrative.h"
#include "log.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

typedef struct {
    char *buf;
    int   size;
    int   cap;
} WriteCtx;

static size_t write_cb(void *ptr, size_t sz, size_t nmemb, void *ctx)
{
    WriteCtx *w = (WriteCtx *)ctx;
    size_t requested = sz * nmemb;
    size_t space = (w->cap > w->size) ? (size_t)(w->cap - w->size - 1) : 0;
    size_t to_copy = (requested < space) ? requested : space;
    if (to_copy > 0) {
        memcpy(w->buf + w->size, ptr, to_copy);
        w->size += (int)to_copy;
        w->buf[w->size] = '\0';
    }
    /* Bug #2 fix: always return original requested size so libcurl doesn't
       think data was lost. If we truncated, the caller (api_chat) detects
       it via buffer-full check after curl_easy_perform returns. */
    return requested;
}

void api_init(ApiClient *api, const char *endpoint, const char *key, const char *model)
{
    memset(api, 0, sizeof(*api));
    safe_strcpy(api->endpoint, endpoint, MAX_ENDPOINT_LEN);
    safe_strcpy(api->api_key,  key,      MAX_KEY_LEN);
    safe_strcpy(api->model,    model,    MAX_MODEL_LEN);
    api->endpoint[MAX_ENDPOINT_LEN - 1] = '\0';
    api->api_key[MAX_KEY_LEN - 1]       = '\0';
    api->model[MAX_MODEL_LEN - 1]       = '\0';
    /* Token counters are already zero from memset */
}

const char *api_last_error(const ApiClient *api)
{
    return api->last_error;
}

/* 从 JSON 提取 "content" 字段 */
static int extract_content(const char *json, char *out, int out_size)
{
    const char *p = strstr(json, "\"content\"");
    if (!p) p = strstr(json, "\"content\": ");
    if (!p) {
        const char *m = strstr(json, "\"message\"");
        if (m) {
            const char *c = strstr(m, "\"content\"");
            if (c) p = c;
        }
    }
    if (!p) {
        /* Bug #1 fix: handle small out_size gracefully */
        if (out_size <= 0) return 0;
        int copy_len = (int)strlen(json);
        if (copy_len > out_size - 1) copy_len = out_size - 1;
        memcpy(out, json, copy_len);
        out[copy_len] = '\0';
        return copy_len;
    }

    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    p = strchr(p, '"');
    if (!p) return 0;
    p++;

    int i = 0;
    while (*p && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            char c = *(p + 1);
            if      (c == 'n')  out[i++] = '\n';
            else if (c == 't')  out[i++] = '\t';
            else if (c == 'r')  out[i++] = '\r';
            else if (c == '"')  out[i++] = '"';
            else if (c == '\\') out[i++] = '\\';
            else { out[i++] = '\\'; out[i++] = c; }
            p += 2;
        } else if (*p == '"') {
            break;
        } else if (*p == '\0') {
            /* Skip embedded null bytes — some APIs (e.g. 通义千问)
               may inject \\u0000 or raw null bytes in the response. */
            log_warn("API: null byte in content at offset %d, skipping", i);
            p++;
        } else {
            out[i++] = *p;
            p++;
        }
    }
    out[i] = '\0';
    return i;
}

/* Escape a string for inclusion in a JSON string value.
   Uses explicit src_len (NOT null-termination) so embedded \\0 bytes
   cannot cause silent truncation.  Returns the number of bytes written
   (excluding null terminator).  If the buffer is too small, the output
   is truncated at dst_size-1 and the return value exceeds dst_size —
   callers MUST check this.

   JSON rules (RFC 8259): MUST escape " \ and control chars U+0000–U+001F.
   Also escapes / for maximum compatibility (some services require it).
   UTF-8 multi-byte sequences (0x80–0xFF) pass through unchanged —
   they are valid JSON as-is and do not need \\u escaping. */
static int escape_json(const char *src, int src_len, char *dst, int dst_size)
{
    int j = 0;
    for (int i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];

        /* Compute how many bytes this character needs after escaping */
        int need = 1;
        if      (c == '"' || c == '\\')             need = 2;
        else if (c == '/')                           need = 2;  /* \\/ */
        else if (c == '\n' || c == '\r' || c == '\t') need = 2;
        else if (c == '\b' || c == '\f')             need = 2;
        else if (c < 0x20)                           need = 6;  /* \\u00XX */

        /* Only write if there's room (leave space for null) */
        if (j + need < dst_size) {
            if (c == '"' || c == '\\') {
                dst[j++] = '\\';
                dst[j++] = c;
            } else if (c == '/') {
                dst[j++] = '\\';
                dst[j++] = '/';
            } else if (c == '\n') {
                dst[j++] = '\\'; dst[j++] = 'n';
            } else if (c == '\r') {
                dst[j++] = '\\'; dst[j++] = 'r';
            } else if (c == '\t') {
                dst[j++] = '\\'; dst[j++] = 't';
            } else if (c == '\b') {
                dst[j++] = '\\'; dst[j++] = 'b';
            } else if (c == '\f') {
                dst[j++] = '\\'; dst[j++] = 'f';
            } else if (c < 0x20) {
                /* Control char (including \\0) → \\u00XX.
                   snprintf is safe here — dst has room (checked above). */
                char hex[7];
                snprintf(hex, sizeof(hex), "\\u%04x", c);
                for (int k = 0; hex[k]; k++) dst[j++] = hex[k];
            } else {
                /* Printable ASCII or UTF-8 continuation byte — pass through */
                dst[j++] = (char)c;
            }
        } else {
            /* Buffer full — keep counting but skip writing.
               This ensures the return value is the TOTAL length needed,
               which callers use to detect truncation. */
            j += need;
        }
    }
    /* Null-terminate at most dst_size-1 */
    if (j < dst_size)
        dst[j] = '\0';
    else if (dst_size > 0)
        dst[dst_size - 1] = '\0';
    return j;
}

/* Extract token usage from OpenAI-compatible API response JSON.
   Parses the "usage" object and accumulates into the ApiClient counters. */
static void api_accumulate_tokens(ApiClient *api, const char *response_json)
{
    if (!response_json || !response_json[0]) return;

    /* Locate "usage":{...} block */
    const char *u = strstr(response_json, "\"usage\"");
    if (!u) return;
    u = strchr(u, '{');
    if (!u) return;

    int prompt = 0, completion = 0, total = 0;
    json_get_int(u, "prompt_tokens",     &prompt);
    json_get_int(u, "completion_tokens", &completion);
    json_get_int(u, "total_tokens",      &total);

    if (total > 0) {
        api->total_prompt_tokens      += prompt;
        api->total_completion_tokens  += completion;
        api->total_tokens             += total;
        api->api_call_count++;

        log_info("API: tokens this call: prompt=%d completion=%d total=%d | "
                 "cumulative: prompt=%lld completion=%lld total=%lld (calls=%d)",
                 prompt, completion, total,
                 api->total_prompt_tokens, api->total_completion_tokens,
                 api->total_tokens, api->api_call_count);
    }
}

bool api_chat(ApiClient *api, const char *system_prompt,
              const char *user_prompt, char *out, int out_size,
              int max_tokens)
{
    log_info("API: chat request (model=%s, max_tokens=%d, out_buf=%d, "
             "sys_len=%d, usr_len=%d)",
             api->model, max_tokens, out_size,
             (int)strlen(system_prompt), (int)strlen(user_prompt));
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(api->last_error, sizeof(api->last_error), "curl_easy_init failed");
        log_error("API: curl_easy_init failed");
        return false;
    }

    /* ── Escape JSON strings with heap buffers sized to input ──
       Worst-case: every byte is a control char → 6x expansion (\\u00XX).
       In practice 3x is safe (newlines → \\n = 2x, printable chars = 1x).
       We use 3x + 256 margin. */
    int sys_in_len  = (int)strlen(system_prompt);
    int usr_in_len  = (int)strlen(user_prompt);

    /* Detect embedded null bytes that would cause silent truncation.
       strlen stops at the first \\0, but the underlying buffer may be
       larger.  If the source pointer came from a buffer with a known
       size we could check more rigorously; here we verify that the
       null-terminated view is self-consistent. */
    if (sys_in_len > 0 && system_prompt[sys_in_len] != '\0') {
        log_warn("API: system_prompt has data after null terminator at offset %d",
                 sys_in_len);
    }
    if (usr_in_len > 0 && user_prompt[usr_in_len] != '\0') {
        log_warn("API: user_prompt has data after null terminator at offset %d",
                 usr_in_len);
    }

    int sys_esc_sz  = sys_in_len * 3 + 256;
    int usr_esc_sz  = usr_in_len * 3 + 256;
    if (sys_esc_sz < 4096)  sys_esc_sz = 4096;   /* reasonable floor */
    if (usr_esc_sz < 16384) usr_esc_sz = 16384;
    if (sys_esc_sz > 131072) sys_esc_sz = 131072; /* sanity cap */
    if (usr_esc_sz > 131072) usr_esc_sz = 131072;

    char *sys_esc = (char*)malloc(sys_esc_sz);
    char *usr_esc = (char*)malloc(usr_esc_sz);
    if (!sys_esc || !usr_esc) {
        snprintf(api->last_error, sizeof(api->last_error),
            "out of memory for escape buffers");
        log_error("API: malloc failed for escape buffers (sys=%d usr=%d)",
                  sys_esc_sz, usr_esc_sz);
        free(sys_esc); free(usr_esc);
        curl_easy_cleanup(curl);
        return false;
    }

    int sys_esc_len = escape_json(system_prompt, sys_in_len, sys_esc, sys_esc_sz);
    int usr_esc_len = escape_json(user_prompt,   usr_in_len, usr_esc, usr_esc_sz);

    /* Detect truncation in either escape buffer */
    if (sys_esc_len >= sys_esc_sz) {
        snprintf(api->last_error, sizeof(api->last_error),
            "system prompt too large after escaping (%d -> %d, buf=%d)",
            sys_in_len, sys_esc_len, sys_esc_sz);
        log_error("API: sys_esc truncated (%d/%d)", sys_esc_len, sys_esc_sz);
        free(sys_esc); free(usr_esc);
        curl_easy_cleanup(curl);
        return false;
    }
    if (usr_esc_len >= usr_esc_sz) {
        snprintf(api->last_error, sizeof(api->last_error),
            "user prompt too large after escaping (%d -> %d, buf=%d)",
            usr_in_len, usr_esc_len, usr_esc_sz);
        log_error("API: usr_esc truncated (%d/%d), "
                  "game state may be too large", usr_esc_len, usr_esc_sz);
        free(sys_esc); free(usr_esc);
        curl_easy_cleanup(curl);
        return false;
    }

    /* Compute exact JSON template overhead by snprintf to NULL.
       We measure the fixed parts (model name, boilerplate, max_tokens)
       and add the already-allocated escaped strings. This avoids the
       guesswork of a fixed +1024 margin. */
    int body_overhead = snprintf(NULL, 0,
        "{"
        "\"model\":\"%s\","
        "\"messages\":["
            "{\"role\":\"system\",\"content\":\"%s\"},"
            "{\"role\":\"user\",\"content\":\"%s\"}"
        "],"
        "\"temperature\":0.7,"
        "\"max_tokens\":%d"
        "}",
        api->model, "", "", max_tokens);
    /* snprintf(NULL, 0, ...) returns the number of chars that WOULD be
       written, excluding null terminator. Add 1 for the null. */
    int body_sz = body_overhead + 1 + sys_esc_len + usr_esc_len;
    char *body = (char*)malloc(body_sz);
    if (!body) {
        snprintf(api->last_error, sizeof(api->last_error),
            "out of memory for request body");
        log_error("API: malloc failed for body (%d bytes)", body_sz);
        free(sys_esc); free(usr_esc);
        curl_easy_cleanup(curl);
        return false;
    }

    int body_len = snprintf(body, body_sz,
        "{"
        "\"model\":\"%s\","
        "\"messages\":["
            "{\"role\":\"system\",\"content\":\"%s\"},"
            "{\"role\":\"user\",\"content\":\"%s\"}"
        "],"
        "\"temperature\":0.7,"
        "\"max_tokens\":%d"
        "}",
        api->model, sys_esc, usr_esc, max_tokens);

    /* Body truncation detection (should never happen with exact sizing,
       but kept as a defensive check) */
    if (body_len < 0 || body_len >= body_sz) {
        snprintf(api->last_error, sizeof(api->last_error),
            "request body too large (%d bytes, limit=%d)", body_len, body_sz);
        log_error("API: body build failed (len=%d, sz=%d)", body_len, body_sz);
        free(body); free(sys_esc); free(usr_esc);
        curl_easy_cleanup(curl);
        return false;
    }

    /* Escape buffers no longer needed after body is built */
    free(sys_esc); sys_esc = NULL;
    free(usr_esc); usr_esc = NULL;

    struct curl_slist *headers = NULL;
    char auth[384];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api->api_key);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Expect:");

    WriteCtx ctx = { .buf = out, .size = 0, .cap = out_size };

    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", api->endpoint);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    /* CA 证书：exe 同目录 cacert.pem */
    {
        char exe_path[512], *slash;
        GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
        slash = strrchr(exe_path, '\\');
        if (slash) *slash = '\0';
        char ca_path[512];
        snprintf(ca_path, sizeof(ca_path), "%s\\cacert.pem", exe_path);
        if (GetFileAttributesA(ca_path) != INVALID_FILE_ATTRIBUTES)
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        snprintf(api->last_error, sizeof(api->last_error),
            "curl error: %s", curl_easy_strerror(res));
        log_error("API: curl_easy_perform failed: %s", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(body);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    log_info("API: HTTP %ld, response=%d bytes", http_code, ctx.size);
    if (http_code != 200) {
        /* ── Detailed error logging ── */

        /* 1. Log full response body (first 500 chars) for diagnosis */
        log_error("API: HTTP %ld — response body (%.500s)",
                  http_code, out[0] ? out : "(empty)");

        /* 2. Try to extract "error"."message" from JSON response */
        {
            const char *err_key = strstr(out, "\"error\"");
            if (err_key) {
                const char *msg_key = strstr(err_key, "\"message\"");
                if (msg_key) {
                    msg_key = strchr(msg_key, ':');
                    if (msg_key) {
                        msg_key++; /* skip ':' */
                        while (*msg_key == ' ' || *msg_key == '"') msg_key++;
                        char err_msg[256] = {0};
                        int ei = 0;
                        while (*msg_key && *msg_key != '"' && ei < 250) {
                            if (*msg_key == '\\' && msg_key[1] == '"')
                                { err_msg[ei++] = '"'; msg_key += 2; continue; }
                            err_msg[ei++] = *msg_key++;
                        }
                        err_msg[ei] = '\0';
                        if (err_msg[0])
                            log_error("API: error.message = \"%s\"", err_msg);
                    }
                }
            }
        }

        /* 3. Log request body (first 300 chars, API key masked) */
        if (body) {
            /* Mask the API key if it appears in the body (it shouldn't in
               a POST body, but be defensive) */
            char *body_safe = strstr(body, api->api_key);
            if (body_safe && api->api_key[0]) {
                /* Replace key with *** in a copy before logging */
                int body_len = (int)strlen(body);
                char *body_copy = (char*)malloc(body_len + 1);
                if (body_copy) {
                    memcpy(body_copy, body, body_len + 1);
                    char *pos = strstr(body_copy, api->api_key);
                    if (pos) memset(pos, '*', strlen(api->api_key));
                    log_error("API: request body (%.300s)", body_copy);
                    free(body_copy);
                }
            } else {
                log_error("API: request body (%.300s)", body);
            }
        }

        /* 4. Build user-facing error message */
        /* If it's an HTML error page, extract just the title */
        const char *detail = out;
        char short_msg[256] = {0};
        if (strstr(out, "<html>") || strstr(out, "<HTML>")) {
            const char *title = strstr(out, "<title>");
            if (title) {
                title += 7;
                const char *end = strstr(title, "</title>");
                int len = end ? (int)(end - title) : 60;
                if (len > 200) len = 200;
                safe_strcpy(short_msg, title, len + 1);
                short_msg[len] = '\0';
                detail = short_msg;
            } else {
                detail = "(服务器返回 HTML 错误页)";
            }
        }
        /* Truncate long messages */
        char truncated[256];
        int dlen = (int)strlen(detail);
        if (dlen > 200) {
            safe_strcpy(truncated, detail, 201);
            truncated[200] = '\0';
            detail = truncated;
        }
        snprintf(api->last_error, sizeof(api->last_error),
            "HTTP %ld: %s", http_code, detail);
        if (http_code == 403)
            strncat(api->last_error,
                "  [请检查: API密钥/端点地址/模型名称是否正确]",
                sizeof(api->last_error) - strlen(api->last_error) - 1);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(body);
        return false;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    /* Bug #2 fix: detect response truncation */
    if (ctx.size >= out_size - 1 && out_size > 1) {
        snprintf(api->last_error, sizeof(api->last_error),
            "response too large (truncated at %d bytes)", ctx.size);
        log_error("API: response truncated (%d bytes)", ctx.size);
        free(body);
        return false;
    }

    char raw[8192];
    strncpy(raw, out, sizeof(raw) - 1);
    raw[sizeof(raw) - 1] = '\0';
    api_accumulate_tokens(api, raw);
    extract_content(raw, out, out_size);
    log_info("API: chat OK — content extracted: %d chars", (int)strlen(out));
    free(body);
    return true;
}

/* 解析单行标记 — 取 tag 后到行尾的内容 */
static bool parse_tag(const char *text, const char *tag, char *out, int out_size)
{
    /* Find tag at line start (\n prefix) or beginning of text */
    /* Bug #4 fix: don't skip past valid matches when tag appears mid-text */
    const char *p = text;
    while (p && *p) {
        p = strstr(p, tag);
        if (!p) { out[0] = '\0'; return false; }
        /* Verify tag is at line start */
        if (p == text || *(p - 1) == '\n') break;
        p++; /* skip one char to continue searching */
    }
    p += strlen(tag);
    while (*p == ' ' || *p == '\t') p++;
    const char *end = strchr(p, '\n');
    int len = end ? (int)(end - p) : (int)strlen(p);
    /* Trim trailing \r */
    while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ')) len--;
    if (len < 0 || len > out_size - 1) {
        log_error("parse_tag: invalid length %d (out_size=%d)", len, out_size);
        out[0] = '\0';
        return false;
    }
    if (len == 0) { out[0] = '\0'; return false; }
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/* Case-insensitive strstr helper — returns pointer to first match or NULL */
static const char *stristr(const char *haystack, const char *needle)
{
    int nlen = (int)strlen(needle);
    if (nlen == 0) return haystack;
    for (const char *p = haystack; *p; p++) {
        int i;
        for (i = 0; i < nlen; i++) {
            char h = (char)((p[i] >= 'A' && p[i] <= 'Z') ? p[i] + 32 : p[i]);
            char n = (char)((needle[i] >= 'A' && needle[i] <= 'Z') ? needle[i] + 32 : needle[i]);
            if (!p[i] || h != n) break;
        }
        if (i == nlen) return p;
    }
    return NULL;
}

/* 解析多行块 — 从 tag 后取到下一个标签（标签以 \n 开头即位于行首）
   Safe version: validates all pointer arithmetic against null terminator
   and never passes NULL to strstr/stristr. */
static bool parse_block(const char *text, const char *tag,
                        const char **next_tags, int ntags,
                        char *out, int out_size)
{
    out[0] = '\0';
    if (!text || !tag || !out) return false;

    const char *start = strstr(text, tag);
    if (!start) start = stristr(text, tag);
    if (!start) return false;

    const char *p = start + strlen(tag);
    /* Skip whitespace, but never go past the null terminator */
    while (*p == ' ' || *p == '\n' || *p == '\r') {
        if (*p == '\0') return false;
        p++;
    }
    if (*p == '\0') return false;

    /* Find the earliest next tag that appears at a line start.
       Search from p (the content) forward; stop at the first line-start match. */
    const char *end = text + strlen(text);
    for (int i = 0; i < ntags; i++) {
        const char *next = strstr(p, next_tags[i]);
        if (next && next < end) {
            /* Verify the tag is at line start */
            if (next == p || *(next - 1) == '\n') {
                end = next;
                break;
            }
        }
    }

    if (end <= p) return false;
    int len = (int)(end - p);
    /* Trim trailing whitespace */
    while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' '))
        len--;
    if (len <= 0) return false;
    if (len >= out_size) len = out_size - 1;
    /* Walk back to UTF-8 character boundary to avoid splitting multi-byte chars */
    while (len > 0 && ((unsigned char)p[len] & 0xC0) == 0x80)
        len--;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/* ── 第一步：变量筛选（轻量调用，只传变量名目录）── */

bool api_select_vars(ApiClient *api, const char *user_input,
                     const char *var_catalog, VarSelectResult *result)
{
    log_info("API: select_vars (input=%.40s)", user_input);
    memset(result, 0, sizeof(*result));

    const char *sys =
        "你是一个游戏状态分析器。根据玩家输入，从可用的游戏变量中选出与当前情境相关的变量。\n"
        "只返回如下格式，不要任何额外文字：\n"
        "VARS: 变量名1,变量名2,变量名3\n"
        "CHARS: 人物名1,人物名2\n\n"
        "规则：\n"
        "- VARS: 从下方目录中选出玩家操作可能影响或需要参考的变量名，选最相关的3-8个\n"
        "- CHARS: 列出玩家输入中明确提到的人物名，无则留空\n"
        "- 变量名必须与目录中的名称完全一致\n"
        "- 天气、时间、地点三者通常都应被选中（它们影响所有场景）\n"
        "- 如果玩家使用特定技能，务必选中对应技能变量\n"
        "- 如果涉及社交互动，选中相关NPC的好感度变量\n"
        "- 所有内容使用中文";

    char prompt[16384];
    snprintf(prompt, sizeof(prompt),
        "用户输入：%s\n\n"
        "变量名目录：\n%s\n\n"
        "请筛选。", user_input, var_catalog);

    char raw[4096];
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 512)) {
        return false;
    }
    safe_strcpy(result->raw, raw, sizeof(result->raw));

    char *vars_line  = strstr(raw, "VARS:");
    char *chars_line = strstr(raw, "CHARS:");

    if (vars_line) {
        vars_line += 5;
        while (*vars_line == ' ') vars_line++;
        char *end = strchr(vars_line, '\n');
        if (!end) end = vars_line + strlen(vars_line);
        int len = (int)(end - vars_line);
        if (len > (int)sizeof(result->var_names) - 1) len = sizeof(result->var_names) - 1;
        strncpy(result->var_names, vars_line, len);
        result->var_names[len] = '\0';
        char *cr = strchr(result->var_names, '\r');
        if (cr) *cr = '\0';
    }

    if (chars_line) {
        chars_line += 6;
        while (*chars_line == ' ') chars_line++;
        char *end = strchr(chars_line, '\n');
        if (!end) end = chars_line + strlen(chars_line);
        int len = (int)(end - chars_line);
        if (len > (int)sizeof(result->char_names) - 1) len = sizeof(result->char_names) - 1;
        strncpy(result->char_names, chars_line, len);
        result->char_names[len] = '\0';
        char *cr = strchr(result->char_names, '\r');
        if (cr) *cr = '\0';
    }

    return true;
}

/* ── 第二步：完整生成（只传被选中变量的值）── */

bool api_generate(ApiClient *api, const char *user_input,
                  const char *selected_vars, FullResponse *result)
{
    log_info("API: generate (input=%.40s)", user_input);
    memset(result, 0, sizeof(*result));
    result->time_advance = -1;
    result->weather = -1;

    /* ── Phase 4: Narrative prompt from narrative module ── */
    char sys_prompt[16384];
    snprintf(sys_prompt, sizeof(sys_prompt),
        "%s\n\n"
        "[输出格式]\n"
        "严格按以下格式回复。每个标签独占一行，标签之间用空行分隔，不要遗漏任何适用的标签：\n\n"
        "TEXT: <叙事/对话内容>\n\n"
        "CHANGES:\n"
        "<变量变更，每行一个。格式: 变量名=新值 或 变量名+增量 或 变量名-减量>\n"
        "  好感度增减: NPC名.player_affinity+数值 或 NPC名.player_affinity-数值\n"
        "  好感度变更幅度不宜超过±10\n"
        "  关系变更: NPC名.relation.玩家名=关系类型+/-好感度\n"
        "  关系类型必须从以下16种中选择（括号内为中文含义）:\n"
        "    PARENT(父母) CHILD(子女) SIBLING(兄弟姐妹) SPOUSE(配偶)\n"
        "    LOVER(恋人) EX(前任) KIN(亲戚) FRIEND(朋友)\n"
        "    BEST_FRIEND(挚友) RIVAL(对手) ENEMY(仇敌) COLLEAGUE(同事)\n"
        "    MASTER(师父) DISCIPLE(徒弟) STRANGER(陌生人) ACQUAINTANCE(熟人)\n\n"
        "ACTION_PROPOSAL:\n"
        "<JSON格式的结构化变更提案，供规则引擎处理。每行动一个JSON对象，可多行也可单行数组>\n"
        "  格式示例: {\"entity\":\"player\",\"field\":\"money\",\"delta\":-50}\n"
        "  操作类型: \"change\"=增减 \"set\"=设定值 \"status\"=状态变更 \"str\"=字符串设定\n"
        "  无结构化变更需求时留空>\n\n"
        "TIME: <经过的分钟数，如 30 表示过了30分钟，不推进写 0>\n\n"
        "WEATHER: <天气代码: 0晴 1多云 2阴天 3小雨 4大雨 5雷暴 6雪 7暴风雪 8雾 9大风 10沙尘暴，不变写 -1>\n\n"
        "LOCATION: <新地点，格式: 大地点/小地点/具体地点，不变留空>\n\n"
        "【时代约束】\n"
        "- 所有叙事、对话、事件必须严格符合当前时代背景\n"
        "- NPC的行为、语言、持有的物品、使用的工具必须与时代一致\n\n"
        "【时间与NPC行为】\n"
        "- 必须根据当前游戏时间决定NPC的行为和出场\n"
        "- NPC_SPAWN和NPC_CREATE必须考虑当前时间是否合理\n\n"
        "MEM_SUMMARY: <本次事件≤10字总结，无值得记录的内容则留空>\n\n"
        "MEM_SHORT: <NPC对话内容摘要，存入短期记忆(8小时)。不添加留空>\n\n"
        "MEM_LONG: <事件记录，存入长期记忆(30天)。不添加留空>\n\n"
        "MEM_PERMANENT: <重大事件，存入永久记忆。不添加留空>\n\n"
        "MEM_CHARS: <以上记忆关联的人物名，逗号分隔>\n\n"
        "NPC_TEMP: <临时NPC，格式: 名称|简短描述，每行一个。无则留空>\n\n"
        "NPC_SPAWN: <有角色卡的NPC出场判定，每行一个。格式: NPC名称|MUST或MAYBE或NEVER|出现地点>\n"
        "  MUST=必定出场  MAYBE=可能出场(20%%概率)  NEVER=不出场\n"
        "  判断依据: NPC身份、当前状态、时间地点是否合理\n\n"
        "NPC_CREATE: <新建NPC角色卡，每行 key=value。不需要则留空>\n"
        "  必填: name=常见中文名（使用日常生活中常见的名字，避免生僻字和文艺名）, age=年龄, gender=性别(男/女/其他), personality=身份性格描述(须明确社会身份), clothing=衣着描述(须符合身份)\n"
        "  必填: home=住所地点名(每个NPC必须有家，用世界中的地点名)\n"
        "  选填: status=状态(NORMAL/HUNGRY/TIRED/SICK/INJURED/EXCITED/ANGRY/SAD/HAPPY)\n"
        "        money=金钱数量, attr.appearance=颜值(0-100)\n"
        "        attr.constitution=体质(0-100), attr.intelligence=智力(0-100)\n"
        "        skill.技能名=等级, item.物品名=数量\n"
        "        relation.关联人名=关系类型+/-好感度, player_affinity=好感度(-100~100)\n\n"
        "你需要自主裁决：剧情走向、NPC行为、时间流逝、天气变化、地点转移、\n"
        "角色属性变化、好感度变化、物品得失、技能成长、以及哪些事值得记住。\n"
        "NPC的说话方式、行为选择、事件反应必须严格符合其社会身份和性格。",
        narrative_get_system_prompt());

    const char *sys = sys_prompt;

    char prompt[32768];
    snprintf(prompt, sizeof(prompt),
        "玩家输入：%s\n\n"
        "游戏状态：\n%s\n\n"
        "请裁决接下来的发展。", user_input, selected_vars);

    char raw[16384];
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 4096)) {
        return false;
    }
    log_info("API: generate: api_chat OK, raw length=%d, preview=%.200s",
             (int)strlen(raw), raw);
    safe_strcpy(result->raw, raw, sizeof(result->raw));

    const char *tags[] = {
        "\nTEXT:", "\nCHANGES:", "\nTIME:", "\nWEATHER:",
        "\nLOCATION:", "\nMEM_SUMMARY:", "\nMEM_SHORT:", "\nMEM_LONG:",
        "\nMEM_PERMANENT:", "\nMEM_CHARS:",
        "\nNPC_TEMP:", "\nNPC_SPAWN:", "\nNPC_CREATE:",
        "\nACTION_PROPOSAL:"
    };
    const int ntags = 14;

    parse_block(raw, "TEXT:", tags, ntags, result->text, sizeof(result->text));
    parse_block(raw, "CHANGES:", tags, ntags, result->changes, sizeof(result->changes));

    char buf[64];
    if (parse_tag(raw, "TIME:", buf, sizeof(buf))) {
        int t = 0;
        if (sscanf(buf, "%d", &t) == 1 && t >= 0 && t <= 10080)
            result->time_advance = t;
    }
    if (parse_tag(raw, "WEATHER:", buf, sizeof(buf))) {
        char *endp = NULL;
        long w = strtol(buf, &endp, 10);
        if (endp && endp != buf) {
            while (*endp == ' ' || *endp == '\r' || *endp == '\n') endp++;
            if (*endp == '\0' && w >= -1 && w <= 10)
                result->weather = (int)w;
        }
    }

    parse_tag(raw, "LOCATION:", result->location, sizeof(result->location));
    if (result->location[0] && !strchr(result->location, '/'))
        result->location[0] = '\0';

    parse_tag(raw, "MEM_SUMMARY:", result->mem_summary, sizeof(result->mem_summary));
    parse_tag(raw, "MEM_SHORT:", result->mem_short, sizeof(result->mem_short));
    parse_tag(raw, "MEM_LONG:", result->mem_long, sizeof(result->mem_long));
    parse_tag(raw, "MEM_PERMANENT:", result->mem_permanent, sizeof(result->mem_permanent));
    parse_tag(raw, "MEM_CHARS:", result->mem_chars, sizeof(result->mem_chars));
    parse_block(raw, "NPC_TEMP:", tags, ntags, result->npc_temp, sizeof(result->npc_temp));
    parse_block(raw, "NPC_SPAWN:", tags, ntags, result->npc_spawn, sizeof(result->npc_spawn));
    parse_block(raw, "NPC_CREATE:", tags, ntags, result->npc_create, sizeof(result->npc_create));
    parse_block(raw, "ACTION_PROPOSAL:", tags, ntags, result->action_proposal, sizeof(result->action_proposal));

    /* ── Safety: force null-terminate every string field ── */
    #define SAFE_TERM(field) do { field[sizeof(field)-1] = '\0'; } while(0)
    SAFE_TERM(result->text);
    SAFE_TERM(result->changes);
    SAFE_TERM(result->location);
    SAFE_TERM(result->mem_short);
    SAFE_TERM(result->mem_long);
    SAFE_TERM(result->mem_permanent);
    SAFE_TERM(result->mem_chars);
    SAFE_TERM(result->mem_summary);
    SAFE_TERM(result->npc_temp);
    SAFE_TERM(result->npc_spawn);
    SAFE_TERM(result->npc_create);
    SAFE_TERM(result->action_proposal);
    SAFE_TERM(result->raw);
    #undef SAFE_TERM

    log_info("API: generate OK — text=%d chg=%d time=%d weather=%d loc=%s",
             (int)strlen(result->text), (int)strlen(result->changes),
             result->time_advance, result->weather,
             result->location[0] ? result->location : "(none)");
    return true;
}

/* ── 旅行时间查询 ── */
bool api_query_travel(ApiClient *api, double distance_km,
                      const char *method, int *out_minutes)
{
    *out_minutes = 0;

    const char *sys =
        "你是一个旅行时间计算器。根据距离和交通方式，估算所需时间。\n"
        "只返回一个整数（分钟数），不要任何其他文字。\n"
        "参考速度：步行5km/h，骑马15km/h，马车10km/h，传送0分钟（法师传送阵）。\n"
        "考虑地形、天气等因素微调。";

    char prompt[512];
    snprintf(prompt, sizeof(prompt),
        "距离: %.1f km\n方式: %s\n\n请估算旅行时间（分钟）。", distance_km, method);

    char raw[256];
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 128)) {
        return false;
    }

    *out_minutes = atoi(raw);
    if (*out_minutes <= 0) *out_minutes = (int)(distance_km / 5.0 * 60); /* 步行兜底 */
    return true;
}

/* ── 世界创建 ── */
bool api_create_world(ApiClient *api,
    const char *name, const char *age, const char *gender,
    const char *clothing, const char *money,
    const char *appearance, const char *constitution,
    const char *intelligence, const char *skills, const char *items,
    const char *story, WorldCreateResult *result)
{
    log_info("API: create_world (name=%s)", name);
    memset(result, 0, sizeof(*result));

    const char *sys =
        "你是一个游戏世界创建者。根据用户提供的角色数据和故事，创建完整的世界。\n\n"
        "【重要】所有地名、NPC名字必须使用中文风格，严禁使用英文名。\n"
        "NPC名字使用日常生活中常见的名字即可，避免生僻字和过于文艺的名字。\n\n"
        "用户的角色卡有所有字段但无性格(personality)，玩家角色卡不应有性格字段。\n\n"
        "严格按以下格式返回：\n\n"
        "PLAYER:\n"
        "<用户角色完整数据，每行 key=value。保留用户填写的字段>\n"
        "必须包含: name, age, gender, clothing, money, status=NORMAL\n"
        "注意: 玩家卡不要写 personality 字段\n"
        "         attr.appearance, attr.constitution, attr.intelligence\n"
        "可选: skill.技能名=等级, item.物品名=数量\n\n"
        "LOCATIONS:\n"
        "<至少15个中文地名，每行: 名称|x|y，坐标0-999，间距≥100，尽量均匀分布覆盖整张地图>\n\n"
        "START:\n"
        "<起始地点 大地点/小地点/具体地点，全部中文>\n\n"
        "TIME:\n"
        "<初始时间，格式: 年|月|日|时|分|星期，如 1|3|15|8|0|星期一>\n"
        "  根据角色故事选择合适的起始年份、季节和时段>\n\n"
        "ERA:\n"
        "<时代背景，约15字以内，简洁概括即可，所有叙事和事件必须符合该时代>\n\n"
        "NPCS:\n"
        "<NPC角色卡，每个用 --- 分隔。每个NPC必须用常见中文名>\n"
        "每个NPC含: name=常见中文名（使用日常生活中常见的名字，避免生僻字和文艺名）, age=年龄, gender=性别(男/女/其他),\n"
        " personality=身份性格描述(必须明确写出此人的社会身份，如'铁匠'、'酒馆老板'、'巡逻队长'等),\n"
        " clothing=衣着描述(必须符合其身份),\n"
        " home=住所地点名(每个NPC必须有家，用地图中已有的地点名),\n"
        " attr.appearance=颜值, attr.constitution=体质,\n"
        " attr.intelligence=智力, player_affinity=好感度(-100~100)\n"
        " 必须包含: relation.玩家名=关系类型+/-好感度\n"
        " 可选: skill.*, item.*\n"
        " 含玩家在内角色卡总数不少于5个，不足则补充合理的中文名NPC>\n"
        " NPC名字使用常见人名即可，避免生僻字和文艺名，同时保持多样，不能重复使用同一姓氏>\n"
        " NPC的对话风格、事件生成必须严格符合其身份和性格>\n\n"
        "关系类型必须从以下16种中选择:\n"
        "PARENT CHILD SIBLING SPOUSE\n"
        "LOVER EX KIN FRIEND\n"
        "BEST_FRIEND RIVAL ENEMY COLLEAGUE\n"
        "MASTER DISCIPLE STRANGER ACQUAINTANCE";

    char prompt[16384];
    int p = 0;
    p += snprintf(prompt + p, sizeof(prompt) - p,
        "【角色卡（无性格，由你根据故事补充）】\n"
        "姓名: %s\n年龄: %s\n性别: %s\n衣着: %s\n金钱: %s\n",
        name, age, gender ? gender : "未设定", clothing, money);
    p += snprintf(prompt + p, sizeof(prompt) - p,
        "颜值: %s  体质: %s  智力: %s\n",
        appearance, constitution, intelligence);
    if (skills && skills[0])
        p += snprintf(prompt + p, sizeof(prompt) - p, "技能: %s\n", skills);
    if (items && items[0])
        p += snprintf(prompt + p, sizeof(prompt) - p, "持有物: %s\n", items);
    p += snprintf(prompt + p, sizeof(prompt) - p,
        "\n【角色故事】\n%s\n\n"
        "请创建世界。至少15个地点、至少5个角色卡(含玩家)。",
        story);

    /* Bug #6 fix: detect truncation of prompt buffer */
    if (p >= (int)sizeof(prompt)) {
        snprintf(api->last_error, sizeof(api->last_error),
            "world create prompt too large");
        return false;
    }

    char raw[32768];
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 4096)) {
        return false;
    }
    safe_strcpy(result->raw, raw, sizeof(result->raw));

    /* 解析区块 */
    char *player_start = strstr(raw, "PLAYER:");
    char *loc_start    = strstr(raw, "LOCATIONS:");
    char *start_start  = strstr(raw, "START:");
    char *time_start   = strstr(raw, "TIME:");
    char *era_start    = strstr(raw, "ERA:");
    char *npc_start    = strstr(raw, "NPCS:");

    /* PLAYER */
    if (player_start) {
        player_start += 7;
        while (*player_start == '\n' || *player_start == '\r') player_start++;
        char *end = loc_start ? loc_start : (start_start ? start_start : (npc_start ? npc_start : NULL));
        if (!end) end = raw + strlen(raw);
        int len = (int)(end - player_start);
        if (len > (int)sizeof(result->player_card) - 1) len = sizeof(result->player_card) - 1;
        while (len > 0 && ((unsigned char)player_start[len] & 0xC0) == 0x80)
            len--;
        strncpy(result->player_card, player_start, len);
        result->player_card[len] = '\0';
        /* 去除尾部空白 */
        char *e = result->player_card + len - 1;
        while (e > result->player_card && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = '\0';
    }

    /* LOCATIONS */
    if (loc_start) {
        loc_start += 10;
        while (*loc_start == '\n' || *loc_start == '\r') loc_start++;
        char *end = start_start ? start_start : (npc_start ? npc_start : NULL);
        if (!end) end = raw + strlen(raw);
        int len = (int)(end - loc_start);
        if (len > (int)sizeof(result->locations) - 1) len = sizeof(result->locations) - 1;
        while (len > 0 && ((unsigned char)loc_start[len] & 0xC0) == 0x80)
            len--;
        strncpy(result->locations, loc_start, len);
        result->locations[len] = '\0';
        char *e = result->locations + len - 1;
        while (e > result->locations && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = '\0';
    }

    /* START */
    if (start_start) {
        start_start += 6;
        while (*start_start == ' ' || *start_start == '\n' || *start_start == '\r') start_start++;
        char *end = time_start ? time_start : (npc_start ? npc_start : (raw + strlen(raw)));
        int len = (int)(end - start_start);
        if (len > (int)sizeof(result->start_location) - 1) len = sizeof(result->start_location) - 1;
        while (len > 0 && ((unsigned char)start_start[len] & 0xC0) == 0x80)
            len--;
        strncpy(result->start_location, start_start, len);
        result->start_location[len] = '\0';
        char *e = result->start_location + len - 1;
        while (e > result->start_location && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = '\0';
    }

    /* TIME */
    if (time_start) {
        time_start += 5;
        while (*time_start == ' ' || *time_start == '\n' || *time_start == '\r') time_start++;
        char *end = era_start ? era_start : (npc_start ? npc_start : (raw + strlen(raw)));
        int len = (int)(end - time_start);
        if (len > (int)sizeof(result->start_time) - 1) len = sizeof(result->start_time) - 1;
        while (len > 0 && ((unsigned char)time_start[len] & 0xC0) == 0x80)
            len--;
        strncpy(result->start_time, time_start, len);
        result->start_time[len] = '\0';
        char *e = result->start_time + len - 1;
        while (e > result->start_time && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = '\0';
    }

    /* ERA */
    if (era_start) {
        era_start += 4;
        while (*era_start == ' ' || *era_start == '\n' || *era_start == '\r') era_start++;
        char *end = npc_start ? npc_start : (raw + strlen(raw));
        int len = (int)(end - era_start);
        if (len > (int)sizeof(result->era) - 1) len = sizeof(result->era) - 1;
        /* Walk back to UTF-8 character boundary */
        while (len > 0 && ((unsigned char)era_start[len] & 0xC0) == 0x80)
            len--;
        strncpy(result->era, era_start, len);
        result->era[len] = '\0';
        char *e = result->era + len - 1;
        while (e > result->era && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = '\0';
    }

    /* NPCS */
    if (npc_start) {
        npc_start += 5;
        while (*npc_start == '\n' || *npc_start == '\r') npc_start++;
        int len = (int)strlen(npc_start);
        if (len > (int)sizeof(result->npc_cards) - 1) len = sizeof(result->npc_cards) - 1;
        while (len > 0 && ((unsigned char)npc_start[len] & 0xC0) == 0x80)
            len--;
        strncpy(result->npc_cards, npc_start, len);
        result->npc_cards[len] = '\0';
        char *e = result->npc_cards + len - 1;
        while (e > result->npc_cards && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = '\0';
    }

    /* ── Safety: force null-terminate every string field ── */
    #define SAFE_TERM(field) do { field[sizeof(field)-1] = '\0'; } while(0)
    SAFE_TERM(result->player_card);
    SAFE_TERM(result->locations);
    SAFE_TERM(result->npc_cards);
    SAFE_TERM(result->start_location);
    SAFE_TERM(result->start_time);
    SAFE_TERM(result->era);
    SAFE_TERM(result->raw);
    #undef SAFE_TERM

    return true;
}

int api_get_token_usage(const ApiClient *api,
                        long long *out_prompt,
                        long long *out_completion,
                        long long *out_total)
{
    if (out_prompt)     *out_prompt     = api->total_prompt_tokens;
    if (out_completion) *out_completion = api->total_completion_tokens;
    if (out_total)      *out_total      = api->total_tokens;
    return api->api_call_count;
}
