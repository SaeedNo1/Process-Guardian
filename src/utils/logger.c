#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE *g_logFile = NULL;
static CRITICAL_SECTION g_cs;

void InitLogger(void) {
    InitializeCriticalSection(&g_cs);
    g_logFile = _wfopen(L"data\\process_guardian.log", L"a");
    if (g_logFile) {
        time_t now = time(NULL);
        wchar_t timeStr[64];
        wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
        fwprintf(g_logFile, L"[%s] Process Guardian started\n", timeStr);
        fflush(g_logFile);
    }
}

void Log(LogLevel level, const wchar_t *format, ...) {
    if (!g_logFile) return;

    EnterCriticalSection(&g_cs);

    time_t now = time(NULL);
    wchar_t timeStr[64];
    wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));

    wchar_t levelStr[16];
    switch (level) {
        case LOG_INFO: wcscpy(levelStr, L"INFO"); break;
        case LOG_WARNING: wcscpy(levelStr, L"WARN"); break;
        case LOG_ERROR: wcscpy(levelStr, L"ERROR"); break;
        default: wcscpy(levelStr, L"INFO"); break;
    }

    fwprintf(g_logFile, L"[%s] [%s] ", timeStr, levelStr);

    va_list args;
    va_start(args, format);
    vfwprintf(g_logFile, format, args);
    va_end(args);

    fwprintf(g_logFile, L"\n");
    fflush(g_logFile);

    LeaveCriticalSection(&g_cs);
}

void CloseLogger(void) {
    if (g_logFile) {
        time_t now = time(NULL);
        wchar_t timeStr[64];
        wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
        fwprintf(g_logFile, L"[%s] Process Guardian stopped\n", timeStr);
        fclose(g_logFile);
        g_logFile = NULL;
    }
    DeleteCriticalSection(&g_cs);
}