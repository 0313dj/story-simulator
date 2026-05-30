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

    /* Switch to exe directory */
    {
        char edir[512];
        GetModuleFileNameA(NULL, edir, sizeof(edir));
        char *slash = strrchr(edir, '\\');
        if (slash) *slash = '\0';
        SetCurrentDirectoryA(edir);
        /* Bug #33: handle CreateDirectoryA failure */
        if (!CreateDirectoryA("saves", NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            /* Non-fatal: saves directory may already exist or be inaccessible.
               Save operations will check and report errors individually. */
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
