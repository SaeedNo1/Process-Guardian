#include "process_protector.h"
#include "../utils/logger.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROTECTED_PROCESSES 256

typedef struct {
    wchar_t name[256];
    BOOL isTree;
    BOOL isRepeated;
} ProtectedEntry;

static ProtectedEntry g_protectedList[MAX_PROTECTED_PROCESSES];
static int g_protectedCount = 0;
static HANDLE g_hMonitorThread = NULL;
static BOOL g_monitorRunning = FALSE;
static HWND g_hMainWindow = NULL;
static CRITICAL_SECTION g_cs;

static const wchar_t* CONFIG_FILE = L"data\\config.dat";

void InitProtector(void) {
    InitializeCriticalSection(&g_cs);
    LoadConfig();
}

void AddToRepeatedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED_PROCESSES) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 255);
        g_protectedList[g_protectedCount].isTree = FALSE;
        g_protectedList[g_protectedCount].isRepeated = TRUE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void AddToRepeatedTreeList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED_PROCESSES) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 255);
        g_protectedList[g_protectedCount].isTree = TRUE;
        g_protectedList[g_protectedCount].isRepeated = TRUE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void AddToProtectedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED_PROCESSES) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 255);
        g_protectedList[g_protectedCount].isTree = FALSE;
        g_protectedList[g_protectedCount].isRepeated = FALSE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void RemoveFromRepeatedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_protectedCount; i++) {
        if (_wcsicmp(g_protectedList[i].name, name) == 0 && g_protectedList[i].isRepeated) {
            for (int j = i; j < g_protectedCount - 1; j++) {
                g_protectedList[j] = g_protectedList[j + 1];
            }
            g_protectedCount--;
            SaveConfig();
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
}

void RemoveFromProtectedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_protectedCount; i++) {
        if (_wcsicmp(g_protectedList[i].name, name) == 0 && !g_protectedList[i].isRepeated) {
            for (int j = i; j < g_protectedCount - 1; j++) {
                g_protectedList[j] = g_protectedList[j + 1];
            }
            g_protectedCount--;
            SaveConfig();
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
}

BOOL IsProcessRepeated(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_protectedCount; i++) {
        if (_wcsicmp(g_protectedList[i].name, name) == 0 && g_protectedList[i].isRepeated) {
            LeaveCriticalSection(&g_cs);
            return TRUE;
        }
    }
    LeaveCriticalSection(&g_cs);
    return FALSE;
}

BOOL IsProcessProtected(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_protectedCount; i++) {
        if (_wcsicmp(g_protectedList[i].name, name) == 0 && !g_protectedList[i].isRepeated) {
            LeaveCriticalSection(&g_cs);
            return TRUE;
        }
    }
    LeaveCriticalSection(&g_cs);
    return FALSE;
}

wchar_t* GetProtectedProcessesList(void) {
    static wchar_t buffer[8192];
    buffer[0] = L'\0';

    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_protectedCount; i++) {
        if (i > 0) wcscat(buffer, L"\n");
        wcscat(buffer, g_protectedList[i].name);
        wcscat(buffer, L" - ");
        if (g_protectedList[i].isRepeated) {
            wcscat(buffer, L"重复终止");
        } else {
            wcscat(buffer, L"保护");
        }
        if (g_protectedList[i].isTree) {
            wcscat(buffer, L" (进程树)");
        }
    }
    LeaveCriticalSection(&g_cs);

    if (buffer[0] == L'\0') {
        return L"无";
    }
    return buffer;
}

static DWORD GetProcessIDByName(const wchar_t *name) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    DWORD pid = 0;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return pid;
}

static BOOL KillProcessByName(const wchar_t *name, BOOL tree) {
    DWORD pid = GetProcessIDByName(name);
    if (pid == 0) return FALSE;

    if (tree) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {0};
            pe.dwSize = sizeof(PROCESSENTRY32W);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ParentProcessID == pid) {
                        KillProcessByName(pe.szExeFile, TRUE);
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }

    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) {
        TerminateProcess(hProc, 1);
        CloseHandle(hProc);
        return TRUE;
    }
    return FALSE;
}

static void LaunchProcess(const wchar_t *name) {
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    if (CreateProcessW(NULL, (LPWSTR)name, NULL, NULL, FALSE, 
                    CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        Log(LOG_INFO, L"已重启保护进程: %s", name);
    }
}

static DWORD WINAPI MonitorThread(LPVOID param) {
    (void)param;
    while (g_monitorRunning) {
        EnterCriticalSection(&g_cs);

        for (int i = 0; i < g_protectedCount; i++) {
            DWORD pid = GetProcessIDByName(g_protectedList[i].name);

            if (g_protectedList[i].isRepeated) {
                if (pid != 0) {
                    KillProcessByName(g_protectedList[i].name, g_protectedList[i].isTree);
                    Log(LOG_INFO, L"重复终止进程: %s", g_protectedList[i].name);
                }
            } else {
                if (pid == 0) {
                    LaunchProcess(g_protectedList[i].name);
                    Log(LOG_INFO, L"保护进程已重启: %s", g_protectedList[i].name);
                }
            }
        }

        LeaveCriticalSection(&g_cs);
        Sleep(500);
    }

    return 0;
}

void StartMonitorThread(HWND hwnd) {
    g_hMainWindow = hwnd;
    if (!g_monitorRunning) {
        g_monitorRunning = TRUE;
        g_hMonitorThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    }
}

void StopMonitorThread(void) {
    g_monitorRunning = FALSE;
    if (g_hMonitorThread) {
        WaitForSingleObject(g_hMonitorThread, 2000);
        CloseHandle(g_hMonitorThread);
        g_hMonitorThread = NULL;
    }
}

void SaveConfig(void) {
    FILE *f = _wfopen(CONFIG_FILE, L"wb");
    if (!f) return;

    fwrite(&g_protectedCount, sizeof(int), 1, f);
    fwrite(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
}

void LoadConfig(void) {
    FILE *f = _wfopen(CONFIG_FILE, L"rb");
    if (!f) return;

    fread(&g_protectedCount, sizeof(int), 1, f);
    fread(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
}