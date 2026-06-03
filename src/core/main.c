#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "backend.h"
#include "httpd.h"
#include "log.h"

/* Convert a wide string to UTF-8. Caller must free the result. */
static char *wchar_to_utf8(const wchar_t *ws)
{
    if (!ws) return NULL;
    int needed = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) return NULL;
    char *s = malloc(needed);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, needed, NULL, NULL);
    return s;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;

    /* ── Parse command-line arguments ── */
    int cli_port = 0;        /* 0 = auto-detect */
    char *cli_webroot = NULL;
    char *cli_saves = NULL;

    {
        int argc_w = 0;
        LPWSTR *argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
        if (argv_w) {
            for (int i = 1; i < argc_w; i++) {
                if (wcscmp(argv_w[i], L"--port") == 0 && i + 1 < argc_w) {
                    cli_port = _wtoi(argv_w[++i]);
                } else if (wcscmp(argv_w[i], L"--webroot") == 0 && i + 1 < argc_w) {
                    cli_webroot = wchar_to_utf8(argv_w[++i]);
                } else if (wcscmp(argv_w[i], L"--saves") == 0 && i + 1 < argc_w) {
                    cli_saves = wchar_to_utf8(argv_w[++i]);
                }
            }
            LocalFree(argv_w);
        }
    }

    /* Switch to exe directory — use Wide APIs for Unicode-safe paths */
    {
        wchar_t edir[512];
        GetModuleFileNameW(NULL, edir, 512);
        wchar_t *slash = wcsrchr(edir, L'\\');
        if (slash) *slash = L'\0';
        if (!SetCurrentDirectoryW(edir)) {
            OutputDebugStringW(L"[sim] SetCurrentDirectoryW failed\n");
        }

        /* Create saves directory using absolute path */
        wchar_t saves_path[576];
        if (cli_saves) {
            /* Use user-specified saves directory */
            int needed = MultiByteToWideChar(CP_UTF8, 0, cli_saves, -1, NULL, 0);
            if (needed > 0 && needed < 576) {
                MultiByteToWideChar(CP_UTF8, 0, cli_saves, -1, saves_path, needed);
            } else {
                swprintf(saves_path, 576, L"%s\\saves", edir);
            }
        } else {
            swprintf(saves_path, 576, L"%s\\saves", edir);
        }
        if (!CreateDirectoryW(saves_path, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                OutputDebugStringW(L"[sim] CreateDirectoryW(saves) failed\n");
            }
        }
    }

    free(cli_webroot);
    free(cli_saves);

    LOG_I("=== Engine Startup ===");
    LOG_I("Initializing backend");

    /* Init game backend */
    srand((unsigned int)time(NULL));
    backend_init();

    LOG_I("Initializing HTTP server");
    /* Start HTTP server — use CLI port if specified, otherwise auto-detect */
    int port;
    if (cli_port > 0 && cli_port <= 65535) {
        port = httpd_start(cli_port);
        if (port < 0) {
            LOG_E("HTTP 服务器在端口 %d 启动失败，尝试自动选择", cli_port);
            port = httpd_autostart();
        } else {
            port = cli_port;
        }
    } else {
        port = httpd_autostart();
    }
    if (port < 0) {
        LOG_E("HTTP 服务器启动失败");
        MessageBoxW(NULL, L"HTTP 服务器启动失败", L"错误", MB_ICONERROR);
        backend_shutdown();
        return 1;
    }
    LOG_I("HTTP 服务器已启动，端口: %d", port);

    /* Open in external browser */
    char url[128];
    snprintf(url, sizeof(url), "http://localhost:%d", port);
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    LOG_I("已在浏览器中打开: %s", url);

    /* Keep process alive */
    MessageBoxW(NULL,
        L"已在浏览器中打开。\n关闭此对话框将退出程序。",
        L"模拟器", MB_OK);

    LOG_I("=== Engine Shutdown ===");
    httpd_stop();
    backend_shutdown();
    return 0;
}
