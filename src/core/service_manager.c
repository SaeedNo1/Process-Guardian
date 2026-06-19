#include "service_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REPEATED_SERVICES 256

static wchar_t g_repeatedDeleteList[MAX_REPEATED_SERVICES][256];
static int g_repeatedDeleteCount = 0;
static CRITICAL_SECTION g_cs;
static BOOL g_csInitialized = FALSE;

// Forward declarations for internal functions
static void SaveServiceRepeatedDeleteConfig(void);
static void LoadServiceRepeatedDeleteConfig(void);

static void InitCS(void) {
    if (!g_csInitialized) {
        InitializeCriticalSection(&g_cs);
        g_csInitialized = TRUE;
        LoadServiceRepeatedDeleteConfig();
    }
}

static const wchar_t* CONFIG_FILE = L"data\\service_config.dat";

SVC_API int GetAllServices(ServiceEntry **outEntries) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSCM) return 0;

    DWORD bytesNeeded = 0;
    DWORD servicesReturned = 0;
    DWORD resumeHandle = 0;
    
    EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, 
                          SERVICE_STATE_ALL, NULL, 0, &bytesNeeded, 
                          &servicesReturned, &resumeHandle, NULL);
    
    if (GetLastError() != ERROR_MORE_DATA) {
        CloseServiceHandle(hSCM);
        return 0;
    }

    BYTE *buffer = (BYTE *)malloc(bytesNeeded);
    if (!buffer) {
        CloseServiceHandle(hSCM);
        return 0;
    }

    if (!EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                                SERVICE_STATE_ALL, buffer, bytesNeeded, 
                                &bytesNeeded, &servicesReturned, &resumeHandle, NULL)) {
        free(buffer);
        CloseServiceHandle(hSCM);
        return 0;
    }

    ServiceEntry *entries = (ServiceEntry *)malloc(servicesReturned * sizeof(ServiceEntry));
    if (!entries) {
        free(buffer);
        CloseServiceHandle(hSCM);
        return 0;
    }
    memset(entries, 0, servicesReturned * sizeof(ServiceEntry));

    ENUM_SERVICE_STATUS_PROCESSW *svc = (ENUM_SERVICE_STATUS_PROCESSW *)buffer;
    int count = 0;
    for (DWORD i = 0; i < servicesReturned; i++) {
        ServiceEntry *entry = &entries[count];
        wcsncpy(entry->name, svc[i].lpServiceName, 255);
        wcsncpy(entry->displayName, svc[i].lpDisplayName, 255);
        
        // Get service config for path and start type
        SC_HANDLE hSvc = OpenServiceW(hSCM, svc[i].lpServiceName, 
                                       SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
        if (hSvc) {
            BYTE configBuf[4096];
            QUERY_SERVICE_CONFIGW *config = (QUERY_SERVICE_CONFIGW *)configBuf;
            DWORD bytesNeeded = 0;
            if (QueryServiceConfigW(hSvc, config, 4096, &bytesNeeded)) {
                if (config->lpBinaryPathName) {
                    wcsncpy(entry->path, config->lpBinaryPathName, 511);
                }
                switch (config->dwStartType) {
                    case SERVICE_AUTO_START:   wcscpy(entry->startTypeStr, L"自动"); entry->startType = 2; break;
                    case SERVICE_DEMAND_START: wcscpy(entry->startTypeStr, L"手动"); entry->startType = 3; break;
                    case SERVICE_DISABLED:     wcscpy(entry->startTypeStr, L"禁用"); entry->startType = 4; break;
                    default:                    wcscpy(entry->startTypeStr, L"未知"); entry->startType = 0; break;
                }
            }
            CloseServiceHandle(hSvc);
        } else {
            entry->path[0] = L'\0';
            entry->startType = 0;
            entry->startTypeStr[0] = L'\0';
        }
        
        // Get status
        if (svc[i].ServiceStatusProcess.dwCurrentState == SERVICE_RUNNING) {
            wcscpy(entry->statusStr, L"运行中");
            entry->status = 4;
        } else if (svc[i].ServiceStatusProcess.dwCurrentState == SERVICE_STOPPED) {
            wcscpy(entry->statusStr, L"已停止");
            entry->status = 1;
        } else if (svc[i].ServiceStatusProcess.dwCurrentState == SERVICE_START_PENDING) {
            wcscpy(entry->statusStr, L"启动中");
            entry->status = 2;
        } else if (svc[i].ServiceStatusProcess.dwCurrentState == SERVICE_STOP_PENDING) {
            wcscpy(entry->statusStr, L"停止中");
            entry->status = 3;
        } else {
            wcscpy(entry->statusStr, L"未知");
            entry->status = 0;
        }
        
        count++;
    }

    free(buffer);
    CloseServiceHandle(hSCM);
    *outEntries = entries;
    return count;
}

SVC_API BOOL StopServiceByName(const wchar_t *serviceName) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return FALSE;

    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName, 
                                   SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return FALSE;
    }

    SERVICE_STATUS status;
    BOOL result = ControlService(hSvc, SERVICE_CONTROL_STOP, &status);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return result || GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
}

SVC_API BOOL StartServiceByName(const wchar_t *serviceName) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return FALSE;

    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName, SERVICE_START);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return FALSE;
    }

    BOOL result = StartServiceW(hSvc, 0, NULL);
    DWORD err = GetLastError();
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return result || err == ERROR_SERVICE_ALREADY_RUNNING;
}

SVC_API BOOL DeleteServiceByName(const wchar_t *serviceName) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return FALSE;

    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName, 
                                   SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return FALSE;
    }

    SERVICE_STATUS status;
    ControlService(hSvc, SERVICE_CONTROL_STOP, &status);
    
    int wait = 0;
    while (wait < 20) {
        if (QueryServiceStatus(hSvc, &status)) {
            if (status.dwCurrentState == SERVICE_STOPPED) break;
        }
        Sleep(250);
        wait++;
    }

    BOOL result = DeleteService(hSvc);
    DWORD err = GetLastError();
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return result || err == ERROR_SUCCESS;
}

SVC_API void AddToRepeatedDeleteList(const wchar_t *serviceName) {
    InitCS();
    EnterCriticalSection(&g_cs);
    if (g_repeatedDeleteCount < MAX_REPEATED_SERVICES) {
        for (int i = 0; i < g_repeatedDeleteCount; i++) {
            if (_wcsicmp(g_repeatedDeleteList[i], serviceName) == 0) {
                LeaveCriticalSection(&g_cs);
                return;
            }
        }
        wcsncpy(g_repeatedDeleteList[g_repeatedDeleteCount], serviceName, 255);
        g_repeatedDeleteList[g_repeatedDeleteCount][255] = L'\0';
        g_repeatedDeleteCount++;
        SaveServiceRepeatedDeleteConfig();
    }
    LeaveCriticalSection(&g_cs);
}

SVC_API void RemoveFromRepeatedDeleteList(const wchar_t *serviceName) {
    InitCS();
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_repeatedDeleteCount; i++) {
        if (_wcsicmp(g_repeatedDeleteList[i], serviceName) == 0) {
            for (int j = i; j < g_repeatedDeleteCount - 1; j++) {
                wcscpy(g_repeatedDeleteList[j], g_repeatedDeleteList[j + 1]);
            }
            g_repeatedDeleteCount--;
            SaveServiceRepeatedDeleteConfig();
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
}

SVC_API BOOL IsServiceRepeatedDelete(const wchar_t *serviceName) {
    InitCS();
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_repeatedDeleteCount; i++) {
        if (_wcsicmp(g_repeatedDeleteList[i], serviceName) == 0) {
            LeaveCriticalSection(&g_cs);
            return TRUE;
        }
    }
    LeaveCriticalSection(&g_cs);
    return FALSE;
}

SVC_API void SaveRepeatedDeleteConfig(void) {
    SaveServiceRepeatedDeleteConfig();
}

SVC_API void LoadRepeatedDeleteConfig(void) {
    LoadServiceRepeatedDeleteConfig();
}

// Internal functions
static void SaveServiceRepeatedDeleteConfig(void) {
    wchar_t configPath[MAX_PATH];
    GetModuleFileNameW(NULL, configPath, MAX_PATH);
    wchar_t *p = wcsrchr(configPath, L'\\');
    if (p) *p = L'\0';
    wcscat(configPath, L"\\");
    wcscat(configPath, CONFIG_FILE);

    FILE *f = _wfopen(configPath, L"wb");
    if (!f) return;
    fwrite(&g_repeatedDeleteCount, sizeof(int), 1, f);
    for (int i = 0; i < g_repeatedDeleteCount; i++) {
        fwrite(g_repeatedDeleteList[i], sizeof(wchar_t), 256, f);
    }
    fclose(f);
}

static void LoadServiceRepeatedDeleteConfig(void) {
    wchar_t configPath[MAX_PATH];
    GetModuleFileNameW(NULL, configPath, MAX_PATH);
    wchar_t *p = wcsrchr(configPath, L'\\');
    if (p) *p = L'\0';
    wcscat(configPath, L"\\");
    wcscat(configPath, CONFIG_FILE);

    FILE *f = _wfopen(configPath, L"rb");
    if (!f) return;
    fread(&g_repeatedDeleteCount, sizeof(int), 1, f);
    for (int i = 0; i < g_repeatedDeleteCount; i++) {
        fread(g_repeatedDeleteList[i], sizeof(wchar_t), 256, f);
    }
    fclose(f);
}
