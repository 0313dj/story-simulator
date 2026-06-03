#include "httpd.h"
#include "backend.h"
#include "json.h"
#include "log.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_running = 0;
static SOCKET g_listen = INVALID_SOCKET;
static char   g_web_root[512];

/* ── Forward declaration ── */
static bool send_all(SOCKET client, const char *data, int len);

/* ── Simple URL-decode ── */
static void urldecode(char *dst, const char *src)
{
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ── MIME type from extension ── */
static const char *mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css")  == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js")   == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    return "application/octet-stream";
}

/* ── Serve a static file ── */
static void serve_file(SOCKET client, const char *path)
{
    /* Per-file serving is too noisy for INFO; keep as DEBUG */
    /* Security: prevent directory traversal */
    /* Bug #26: decode path first, then check for traversal attempts */
    {
        char decoded_path[1024];
        urldecode(decoded_path, path);
        if (strstr(decoded_path, "..") || strstr(decoded_path, "//") ||
            strstr(decoded_path, "\\\\") || strstr(decoded_path, "\\")) {
            LOG_W("HTTP 403: path traversal attempt: %s", path);
            const char *msg = "HTTP/1.1 403 Forbidden\r\n\r\nForbidden";
            send_all(client, msg, (int)strlen(msg));
            return;
        }
        /* Also check for null-byte injection after decode */
        if (strlen(decoded_path) != strlen(path)) {
            /* Path has encoded chars; check the decoded path has no null bytes */
        }
    }

    /* If path is "/", serve index.html */
    if (strcmp(path, "/") == 0) path = "/index.html";

    char full[1024];
    snprintf(full, sizeof(full), "%s%s", g_web_root, path);

    FILE *fp = fopen(full, "rb");
    if (!fp) {
        LOG_W("HTTP 404: %s", full);
        const char *msg = "HTTP/1.1 404 Not Found\r\n\r\nNot Found";
        send_all(client, msg, (int)strlen(msg));
        return;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char header[512];
    int hdr_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n", mime_type(path), sz);
    /* Bug #25 fix: loop send() to handle partial writes */
    {
        int sent = 0;
        while (sent < hdr_len) {
            int n = send(client, header + sent, hdr_len - sent, 0);
            if (n <= 0) { fclose(fp); return; }
            sent += n;
        }
    }

    char buf[8192];
    while (sz > 0) {
        long n = sz > (long)sizeof(buf) ? (long)sizeof(buf) : sz;
        size_t r = fread(buf, 1, (size_t)n, fp);
        if (r > 0) {
            int sent = 0;
            while (sent < (int)r) {
                int ns = send(client, buf + sent, (int)r - sent, 0);
                if (ns <= 0) { fclose(fp); return; }
                sent += ns;
            }
        }
        sz -= (long)r;
    }
    fclose(fp);
}

/* JSON field extraction now uses the shared json.c implementation (fixes Bug #22).
   Callers must pass plain key names (without surrounding quotes). */

/* ── Reliable send: loops until all bytes sent or connection fails ── */
/* Bug #25 fix: handle partial send() by looping */
static bool send_all(SOCKET client, const char *data, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = send(client, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

/* ── Handle POST /api ── */
static void handle_api(SOCKET client, const char *headers, const char *body, int body_len)
{
    (void)headers;
    const char *json = body;

    /* Log request body with API key redacted.
       Never log raw bodies — they may contain "api_key":"sk-..." */
    if (body && body[0]) {
        char safe[512];
        int si = 0, bi = 0;
        while (bi < body_len && si < (int)sizeof(safe) - 1) {
            /* Redact "api_key":"<value>" → "api_key":"***" */
            if (strncmp(body + bi, "\"api_key\"", 9) == 0 ||
                strncmp(body + bi, "\"apiKey\"", 8) == 0 ||
                strncmp(body + bi, "\"key\"", 5) == 0) {
                const char *colon = strchr(body + bi, ':');
                if (colon) {
                    int prefix_len = (int)(colon - (body + bi)) + 1;
                    if (si + prefix_len + 5 < (int)sizeof(safe)) {
                        memcpy(safe + si, body + bi, prefix_len);
                        si += prefix_len;
                        memcpy(safe + si, "\"***\"", 5);
                        si += 5;
                    }
                    /* Skip to after the value's closing quote */
                    const char *v = colon + 1;
                    while (*v == ' ' || *v == '\t') v++;
                    if (*v == '"') {
                        v++;
                        while (*v && *v != '"') v++;
                        if (*v == '"') v++;
                    }
                    bi = (int)(v - body);
                    continue;
                }
            }
            safe[si++] = body[bi++];
        }
        safe[si] = '\0';
        LOG_D("HTTP BODY=%.500s", safe);
    } else {
        LOG_D("HTTP BODY=(empty)");
    }

    char cmd[64] = {0};
    json_get_str(json, "cmd", cmd, sizeof(cmd));
    LOG_I("json_get_str cmd=[%s]", cmd);

    if (!cmd[0]) {
        LOG_W("HTTP API: missing cmd field");
    } else {
        LOG_I("HTTP API cmd=%s (body=%d bytes)", cmd, body_len);
    }

    char text[4096] = {0};
    char fname[256] = {0};

    char *result = NULL;

    if (strcmp(cmd, "get_state") == 0) {
        result = backend_get_state();
    } else if (strcmp(cmd, "get_chat_history") == 0) {
        result = backend_get_chat_history();
    } else if (strcmp(cmd, "send_message") == 0) {
        json_get_str(json, "text", text, sizeof(text));
        result = backend_send_message(text);
    } else if (strcmp(cmd, "quick_save") == 0) {
        result = backend_quick_save();
    } else if (strcmp(cmd, "quick_load") == 0) {
        result = backend_quick_load();
    } else if (strcmp(cmd, "list_saves") == 0) {
        result = backend_list_saves();
    } else if (strcmp(cmd, "load_save") == 0) {
        json_get_str(json, "filename", fname, sizeof(fname));
        result = backend_load_save(fname);
    } else if (strcmp(cmd, "delete_save") == 0) {
        json_get_str(json, "filename", fname, sizeof(fname));
        result = backend_delete_save(fname);
    } else if (strcmp(cmd, "get_api_status") == 0) {
        result = backend_get_api_status();
    } else if (strcmp(cmd, "get_profiles") == 0) {
        result = backend_get_profiles();
    } else if (strcmp(cmd, "save_profile") == 0) {
        char nm[64]={0}, ep[256]={0}, ky[256]={0}, md[64]={0};
        json_get_str(json, "name", nm, sizeof(nm));
        json_get_str(json, "endpoint", ep, sizeof(ep));
        json_get_str(json, "apiKey", ky, sizeof(ky));
        json_get_str(json, "model", md, sizeof(md));
        result = backend_save_profile(nm, ep, ky, md);
    } else if (strcmp(cmd, "delete_profile") == 0) {
        char nm[64] = {0};
        json_get_str(json, "name", nm, sizeof(nm));
        result = backend_delete_profile(nm);
    } else if (strcmp(cmd, "activate_profile") == 0) {
        char nm[64] = {0};
        json_get_str(json, "name", nm, sizeof(nm));
        result = backend_activate_profile(nm);
    } else if (strcmp(cmd, "create_world") == 0) {
        char nm[64]={0}, ag[8]={0}, gd[8]={0}, cl[128]={0}, mn[16]={0};
        char ap[8]={0}, co[8]={0}, in[8]={0}, sk[512]={0}, it[512]={0}, st[2048]={0};
        json_get_str(json, "name", nm, sizeof(nm));
        json_get_str(json, "age", ag, sizeof(ag));
        json_get_str(json, "gender", gd, sizeof(gd));
        json_get_str(json, "clothing", cl, sizeof(cl));
        json_get_str(json, "money", mn, sizeof(mn));
        json_get_str(json, "appearance", ap, sizeof(ap));
        json_get_str(json, "constitution", co, sizeof(co));
        json_get_str(json, "intelligence", in, sizeof(in));
        json_get_str(json, "skills", sk, sizeof(sk));
        json_get_str(json, "items", it, sizeof(it));
        json_get_str(json, "story", st, sizeof(st));
        result = backend_create_world(nm, ag, gd, cl, mn, ap, co, in, sk, it, st);
    } else if (strcmp(cmd, "delete_location") == 0) {
        char lvl[8] = {0}, lname[128] = {0};
        json_get_str(json, "level", lvl, sizeof(lvl));
        json_get_str(json, "name", lname, sizeof(lname));
        LOG_I("HTTP API: delete_location level=%s name=%s", lvl, lname);
        result = backend_delete_location(lvl, lname);
    } else if (strcmp(cmd, "rename_location") == 0) {
        char lvl[8] = {0}, old_name[128] = {0}, new_name[128] = {0};
        json_get_str(json, "level", lvl, sizeof(lvl));
        json_get_str(json, "oldName", old_name, sizeof(old_name));
        json_get_str(json, "newName", new_name, sizeof(new_name));
        LOG_I("HTTP API: rename_location level=%s '%s' → '%s'", lvl, old_name, new_name);
        result = backend_rename_location(lvl, old_name, new_name);
    } else if (strcmp(cmd, "travel") == 0) {
        char fr[64]={0}, to[64]={0}, meth[64]={0};
        double dist = 0;
        json_get_str(json, "from", fr, sizeof(fr));
        json_get_str(json, "to", to, sizeof(to));
        json_get_str(json, "method", meth, sizeof(meth));
        const char *dp = strstr(json, "\"distance\"");
        if (dp) { dp = strchr(dp, ':'); if (dp) dist = atof(dp+1); }
        result = backend_travel(fr, to, dist, meth);
    } else {
        LOG_W("HTTP API: unknown cmd=%.32s", cmd);
        /* Bug #25: use static buffer instead of malloc (avoids leak concern) */
        static const char err_unknown[] = "{\"ok\":false,\"error\":\"未知命令\"}";
        char header[256];
        snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n", (int)sizeof(err_unknown)-1);
        send_all(client, header, (int)strlen(header));
        send_all(client, err_unknown, (int)sizeof(err_unknown)-1);
        return;
    }

    if (result) {
        int result_len = (int)strlen(result);
        LOG_D("HTTP API: cmd=%s -> OK (%d bytes)", cmd, result_len);
        char header[256];
        snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n", result_len);
        send_all(client, header, (int)strlen(header));
        send_all(client, result, (int)strlen(result));
        free(result);
    } else {
        /* Bug #1 fix: backend function returned NULL — send 500 error */
        LOG_E("HTTP API: cmd=%s -> NULL (backend returned nothing)", cmd);
        const char *err_body = "{\"ok\":false,\"error\":\"内部服务器错误\"}";
        char header[256];
        snprintf(header, sizeof(header),
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n", (int)strlen(err_body));
        send_all(client, header, (int)strlen(header));
        send_all(client, err_body, (int)strlen(err_body));
    }
}

/* ── Manual HTTP request-line parser (no sscanf) ── */
static int parse_request_line(const char *buf, char *method, int msz,
                               char *path, int psz)
{
    const char *p = buf;
    /* Skip leading spaces */
    while (*p == ' ' || *p == '\r' || *p == '\n') p++;

    /* Parse method */
    int i = 0;
    while (*p && *p != ' ' && i < msz - 1) method[i++] = *p++;
    method[i] = '\0';
    if (*p != ' ') return -1;

    /* Skip spaces */
    while (*p == ' ') p++;

    /* Parse path */
    i = 0;
    while (*p && *p != ' ' && *p != '?' && i < psz - 1) path[i++] = *p++;
    path[i] = '\0';

    return (i > 0) ? 0 : -1;
}

/* ── Find Content-Length header value ── */
static int get_content_length(const char *headers)
{
    const char *cl = strstr(headers, "Content-Length:");
    if (!cl) cl = strstr(headers, "content-length:");
    if (!cl) return 0;
    cl = strchr(cl, ':');
    if (!cl) return 0;
    cl++;
    while (*cl == ' ') cl++;
    return atoi(cl);
}

/* ── Parse HTTP request and route ── */
static DWORD WINAPI handle_client(LPVOID param)
{
    SOCKET client = (SOCKET)(INT_PTR)param;

    /* Bug #23: set socket receive timeout to prevent hanging when
       Content-Length exceeds actual data. */
    {
        DWORD timeout_ms = 5000; /* 5 seconds */
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout_ms, sizeof(timeout_ms));
    }

    /* Read headers first (up to 16KB, enough for any sane request) */
    char hdr_buf[16384];
    int total = 0;
    while (total < (int)sizeof(hdr_buf) - 1) {
        int n = recv(client, hdr_buf + total,
                     sizeof(hdr_buf) - 1 - total, 0);
        if (n <= 0) { closesocket(client); return 0; }
        total += n;
        hdr_buf[total] = '\0';
        /* Stop when we see end of headers */
        if (strstr(hdr_buf, "\r\n\r\n")) break;
    }
    /* Bug #27: detect header truncation */
    if (total >= (int)sizeof(hdr_buf) - 1 && !strstr(hdr_buf, "\r\n\r\n")) {
        LOG_W("HTTP 431: header too large (%d bytes)", total);
        const char *msg = "HTTP/1.1 431 Request Header Fields Too Large\r\n\r\nHeader too large";
        send_all(client, msg, (int)strlen(msg));
        closesocket(client);
        return 0;
    }

    char method[16] = {0};
    char path[1024] = {0};
    if (parse_request_line(hdr_buf, method, sizeof(method),
                           path, sizeof(path)) != 0 ||
        method[0] == '\0' || path[0] == '\0') {
        LOG_W("HTTP 400: bad request line");
        const char *msg = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send_all(client, msg, (int)strlen(msg));
        closesocket(client);
        return 0;
    }

    urldecode(path, path);

    LOG_D("HTTP %s %s", method, path);

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api") == 0) {
        /* Read body based on Content-Length */
        int cl = get_content_length(hdr_buf);
        /* Bug #23: cap at 64KB — API requests should never be larger */
        if (cl <= 0 || cl > 65536) {
            LOG_W("HTTP 400: invalid Content-Length=%d", cl);
            const char *msg = "HTTP/1.1 400 Bad Request\r\n\r\nInvalid Content-Length";
            send_all(client, msg, (int)strlen(msg));
            closesocket(client);
            return 0;
        }

        char *body = (char *)malloc(cl + 1);
        if (!body) {
            LOG_E("HTTP: malloc(%d) failed for request body", cl + 1);
            closesocket(client); return 0;
        }

        /* Check if body data already came with headers */
        const char *body_start = strstr(hdr_buf, "\r\n\r\n");
        int already = 0;
        if (body_start) {
            body_start += 4;
            already = total - (int)(body_start - hdr_buf);
            if (already < 0) already = 0;
            if (already > cl) already = cl;
            if (already > 0) memcpy(body, body_start, (size_t)already);
        }

        /* Bug #23: read remaining body in a loop with timeout tolerance.
           If Content-Length exceeds actual data, the recv timeout
           will cause partial reads — we accept whatever we get. */
        int received = already;
        while (received < cl) {
            int n = recv(client, body + received, cl - received, 0);
            if (n <= 0) break;  /* timeout or connection closed */
            received += n;
        }
        if (received < cl) {
            LOG_W("HTTP: body short read (got %d, expected %d) — using partial",
                     received, cl);
        }
        body[received] = '\0';

        handle_api(client, hdr_buf, body, received);
        free(body);
    } else if (strcmp(method, "OPTIONS") == 0) {
        const char *resp = "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n\r\n";
        send_all(client, resp, (int)strlen(resp));
    } else if (strcmp(method, "GET") == 0) {
        serve_file(client, path);
    } else {
        LOG_W("HTTP 405: method=%s", method);
        const char *msg = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        send_all(client, msg, (int)strlen(msg));
    }

    /* Graceful shutdown: send FIN, drain, then close */
    shutdown(client, SD_SEND);
    { char tmp[64]; while (recv(client, tmp, sizeof(tmp), 0) > 0) {} }
    closesocket(client);
    return 0;
}

/* ── Accept thread ── */
static DWORD WINAPI accept_thread(LPVOID param)
{
    (void)param;
    while (g_running) {
        SOCKET client = accept(g_listen, NULL, NULL);
        if (client == INVALID_SOCKET) break;
        HANDLE h = CreateThread(NULL, 0, handle_client,
                                (LPVOID)(INT_PTR)client, 0, NULL);
        if (h) CloseHandle(h); else closesocket(client);
    }
    return 0;
}

/* ── Public API ── */

int httpd_start(int port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return -1;

    g_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen == INVALID_SOCKET) { WSACleanup(); return -1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(g_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_E("HTTP server: bind failed on 127.0.0.1:%d", port);
        closesocket(g_listen);
        WSACleanup();
        return -1;
    }

    if (listen(g_listen, 8) != 0) {
        closesocket(g_listen);
        WSACleanup();
        return -1;
    }

    /* Set web root */
    char exe_dir[512];
    GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
    char *slash = strrchr(exe_dir, '\\');
    if (slash) *slash = '\0';
    snprintf(g_web_root, sizeof(g_web_root), "%s\\web", exe_dir);

    LOG_I("HTTP server: web_root=%s", g_web_root);

    g_running = 1;
    CreateThread(NULL, 0, accept_thread, NULL, 0, NULL);

    LOG_I("HTTP server: listening on 127.0.0.1:%d", port);
    return 0;
}

void httpd_stop(void)
{
    LOG_I("HTTP server: stopping...");
    g_running = 0;
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    WSACleanup();
    LOG_I("HTTP server: stopped");
}

/* ── Entry-point helper: find free port and start server ── */

int httpd_autostart(void)
{
    /* Try ports 8765-8775 */
    for (int port = 8765; port <= 8775; port++) {
        if (httpd_start(port) == 0) {
            LOG_I("HTTP autostart: bound to port %d", port);
            return port;
        }
        LOG_W("HTTP autostart: port %d unavailable, trying next...", port);
    }
    LOG_E("HTTP autostart: all ports 8765-8775 are busy");
    return -1;
}
