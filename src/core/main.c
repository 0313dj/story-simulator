#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "backend.h"
#include "httpd.h"
#include "log.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;

    /* Switch to exe directory — use Wide APIs for Unicode-safe paths */
    {
        wchar_t edir[512];
        GetModuleFileNameW(NULL, edir, 512);
        wchar_t *slash = wcsrchr(edir, L'\\');
        if (slash) *slash = L'\0';
        if (!SetCurrentDirectoryW(edir)) {
            /* Working directory change failed — saves/logs will go to
               whatever the current directory happens to be. Continue anyway
               since the web server and game can still function. */
            OutputDebugStringW(L"[sim] SetCurrentDirectoryW failed\n");
        }

        /* Create saves directory using absolute path.
           Using absolute path ensures saves always go next to the exe,
           regardless of whether SetCurrentDirectoryW succeeded. */
        wchar_t saves_path[576];
        swprintf(saves_path, 576, L"%s\\saves", edir);
        /* Bug #33: CreateDirectoryW handles the full path and returns
           ERROR_ALREADY_EXISTS if the directory is already present. */
        if (!CreateDirectoryW(saves_path, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                /* Non-fatal: save operations will check and report errors
                   individually. The saves directory may be on a read-only
                   filesystem or blocked by permissions. */
                OutputDebugStringW(L"[sim] CreateDirectoryW(saves) failed\n");
            }
        }
    }

    /* Init game backend */
    srand((unsigned int)time(NULL));
    backend_init();

    /* Start HTTP server */
    int port = httpd_autostart();
    if (port < 0) {
        log_error("HTTP 服务器启动失败");
        MessageBoxW(NULL, L"HTTP 服务器启动失败", L"错误", MB_ICONERROR);
        backend_shutdown();
        return 1;
    }
    log_info("HTTP 服务器已启动，端口: %d", port);

    /* Open in external browser */
    char url[128];
    snprintf(url, sizeof(url), "http://localhost:%d", port);
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    log_info("已在浏览器中打开: %s", url);

    /* Keep process alive */
    MessageBoxW(NULL,
        L"已在浏览器中打开。\n关闭此对话框将退出程序。",
        L"模拟器", MB_OK);

    log_info("程序退出");
    httpd_stop();
    backend_shutdown();
    return 0;
}
