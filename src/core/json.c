#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void jb_init(JsonBuf *j)
{
    j->cap = 4096;
    j->buf = (char *)malloc(j->cap);
    if (!j->buf) {
        j->cap = 0;
        j->len = 0;
        return;
    }
    j->buf[0] = '\0';
    j->len = 0;
}

void jb_free(JsonBuf *j)
{
    free(j->buf);
    j->buf = NULL;
    j->cap = j->len = 0;
}

char *jb_detach(JsonBuf *j)
{
    char *s = j->buf;
    j->buf = NULL;
    j->cap = j->len = 0;
    return s;
}

static void ensure(JsonBuf *j, int need)
{
    if (!j->buf) return;  /* allocation failed during init */
    if (j->len + need + 1 > j->cap) {
        int new_cap = (j->len + need + 1) * 2;
        char *new_buf = (char *)realloc(j->buf, new_cap);
        if (!new_buf) return;  /* out of memory: keep old buffer */
        j->cap = new_cap;
        j->buf = new_buf;
    }
}

void jb_str(JsonBuf *j, const char *s)
{
    if (!j->buf) return;
    int n = (int)strlen(s);
    ensure(j, n);
    if (!j->buf) return;
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

void jb_esc(JsonBuf *j, const char *s)
{
    if (!j->buf) return;
    /* Worst-case: every byte is a control char → \\u00XX (6 bytes).
       Use 6x + 2 (quotes) + 1 (null) to avoid repeated reallocations. */
    ensure(j, (int)strlen(s) * 6 + 4);
    if (!j->buf) return;
    j->buf[j->len++] = '"';
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':  j->buf[j->len++] = '\\'; j->buf[j->len++] = '"'; break;
        case '\\': j->buf[j->len++] = '\\'; j->buf[j->len++] = '\\'; break;
        case '/':  j->buf[j->len++] = '\\'; j->buf[j->len++] = '/'; break;
        case '\n': j->buf[j->len++] = '\\'; j->buf[j->len++] = 'n'; break;
        case '\r': j->buf[j->len++] = '\\'; j->buf[j->len++] = 'r'; break;
        case '\t': j->buf[j->len++] = '\\'; j->buf[j->len++] = 't'; break;
        case '\b': j->buf[j->len++] = '\\'; j->buf[j->len++] = 'b'; break;
        case '\f': j->buf[j->len++] = '\\'; j->buf[j->len++] = 'f'; break;
        default:
            /* Escape all other control chars (0x00-0x1F) as \\u00XX.
               UTF-8 multi-byte bytes (0x80-0xFF) pass through unchanged. */
            if ((unsigned char)*p < 0x20) {
                ensure(j, 6);
                if (!j->buf) return;
                j->buf[j->len++] = '\\';
                j->buf[j->len++] = 'u';
                j->buf[j->len++] = '0';
                j->buf[j->len++] = '0';
                unsigned char c = (unsigned char)*p;
                j->buf[j->len++] = "0123456789abcdef"[c >> 4];
                j->buf[j->len++] = "0123456789abcdef"[c & 0xF];
            } else {
                j->buf[j->len++] = *p;
            }
            break;
        }
    }
    j->buf[j->len++] = '"';
    j->buf[j->len] = '\0';
}

void jb_fmt(JsonBuf *j, const char *fmt, ...)
{
    if (!j->buf) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    ensure(j, n + 1);
    if (!j->buf) return;
    va_start(ap, fmt);
    vsnprintf(j->buf + j->len, n + 1, fmt, ap);
    va_end(ap);
    j->len += n;
}

void jb_obj_open(JsonBuf *j)  { jb_str(j, "{"); }
void jb_obj_close(JsonBuf *j) { jb_str(j, "}"); }
void jb_arr_open(JsonBuf *j)  { jb_str(j, "["); }
void jb_arr_close(JsonBuf *j) { jb_str(j, "]"); }

void jb_kv_str(JsonBuf *j, const char *key, const char *val)
{
    if (j->buf[j->len - 1] != '{' && j->buf[j->len - 1] != '[')
        jb_str(j, ",");
    jb_esc(j, key);
    jb_str(j, ":");
    jb_esc(j, val);
}

void jb_kv_int(JsonBuf *j, const char *key, int val)
{
    if (j->buf[j->len - 1] != '{' && j->buf[j->len - 1] != '[')
        jb_str(j, ",");
    jb_esc(j, key);
    jb_fmt(j, ":%d", val);
}

void jb_kv_dbl(JsonBuf *j, const char *key, double val)
{
    if (j->buf[j->len - 1] != '{' && j->buf[j->len - 1] != '[')
        jb_str(j, ",");
    jb_esc(j, key);
    jb_fmt(j, ":%.2f", val);
}

void jb_kv_bool(JsonBuf *j, const char *key, int val)
{
    if (j->buf[j->len - 1] != '{' && j->buf[j->len - 1] != '[')
        jb_str(j, ",");
    jb_esc(j, key);
    jb_str(j, val ? ":true" : ":false");
}

void jb_kv_raw(JsonBuf *j, const char *key, const char *raw_json)
{
    if (j->buf[j->len - 1] != '{' && j->buf[j->len - 1] != '[')
        jb_str(j, ",");
    jb_esc(j, key);
    jb_str(j, ":");
    jb_str(j, raw_json);
}

/* ── Flat JSON extraction helpers ── */

static const char *js_find(const char *json, const char *key)
{
    char pat[128];
    int kl = (int)strlen(key);
    if (kl > 120) return NULL;
    pat[0] = '"';
    memcpy(pat + 1, key, kl);
    pat[1 + kl] = '"';
    pat[2 + kl] = ':';
    pat[3 + kl] = '\0';

    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        /* Bug #5 fix: ensure the match is a real JSON key — the character
           before the opening quote must be {, ,, [, whitespace, or start of string */
        if (p == json || (p[-1] == '{' || p[-1] == ',' || p[-1] == '[' ||
                          p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\r' || p[-1] == '\t')) {
            return p;
        }
        p++; /* skip past this false match */
    }
    return NULL;
}

bool json_get_str(const char *json, const char *key, char *out, int out_sz)
{
    const char *p = js_find(json, key);
    if (!p) { out[0] = '\0'; return false; }
    p += (int)strlen(key) + 4;   /* "key":" */
    int i = 0;
    /* Bug #30: handle escaped double-quotes (\") in JSON strings */
    while (*p && i < out_sz - 1) {
        if (*p == '\\' && p[1] == '"') {
            out[i++] = '"';
            p += 2;
        } else if (*p == '\\' && p[1] == '\\') {
            out[i++] = '\\';
            p += 2;
        } else if (*p == '\\' && p[1] == 'n') {
            out[i++] = '\n';
            p += 2;
        } else if (*p == '\\' && p[1] == 'r') {
            out[i++] = '\r';
            p += 2;
        } else if (*p == '\\' && p[1] == 't') {
            out[i++] = '\t';
            p += 2;
        } else if (*p == '"') {
            break;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return true;
}

bool json_get_int(const char *json, const char *key, int *dest)
{
    const char *p = js_find(json, key);
    if (!p) return false;
    p += (int)strlen(key) + 3;   /* "key": */
    *dest = atoi(p);
    return true;
}
