#ifndef JSON_H
#define JSON_H

#include <stdarg.h>
#include <stdbool.h>

/* Simple JSON string builder for C.
   Writes into a dynamically grown buffer. Not a full parser — just enough
   to serialize game state for the JS frontend. */

typedef struct {
    char *buf;
    int   len;
    int   cap;
} JsonBuf;

void  jb_init(JsonBuf *j);
void  jb_free(JsonBuf *j);
char *jb_detach(JsonBuf *j);   /* caller must free() */

void jb_str(JsonBuf *j, const char *s);        /* raw append */
void jb_esc(JsonBuf *j, const char *s);         /* append, JSON-escaped */
void jb_fmt(JsonBuf *j, const char *fmt, ...);  /* printf-style append */

void jb_obj_open(JsonBuf *j);
void jb_obj_close(JsonBuf *j);
void jb_arr_open(JsonBuf *j);
void jb_arr_close(JsonBuf *j);

void jb_kv_str(JsonBuf *j, const char *key, const char *val);
void jb_kv_int(JsonBuf *j, const char *key, int val);
void jb_kv_dbl(JsonBuf *j, const char *key, double val);
void jb_kv_bool(JsonBuf *j, const char *key, int val);
void jb_kv_raw(JsonBuf *j, const char *key, const char *raw_json);

/* Flat JSON string extraction helpers (for parsing simple flat JSON) */
bool json_get_str(const char *json, const char *key, char *out, int out_sz);
bool json_get_int(const char *json, const char *key, int *dest);

#endif
