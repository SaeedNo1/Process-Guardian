#define UNICODE
#define _UNICODE

#include <windows.h>
#include <winuser.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdarg.h>
#include <winioctl.h>
#include <shlobj.h>

// ========== Single instance mutex ==========
#define MUTEX_NAME L"Global\\GuardianDaemonMutex"

// ========== Data structures ==========
// Process protection/repeated kill
typedef struct {
    wchar_t name[260];
    DWORD pid;
    BOOL isTree;
    BOOL isRepeated;
} ProtectedEntry;

#define MAX_PROTECTED_PROCESSES 256

// Service repeated delete
typedef struct {
    wchar_t name[256];
} SvcRepeatedEntry;

#define MAX_SVC_REPEATED 128

// Registry repeated delete
typedef struct {
    wchar_t fullPath[512];
} RegRepeatedEntry;

#define MAX_REG_REPEATED 256

// Partition repeated delete
typedef struct {
    int diskNumber;
    int partitionNumber;
    wchar_t location[512];
} PartRepeatedEntry;

#define MAX_PART_REPEATED 64

// Registry protected list (path -> snapshot file)
typedef struct {
    wchar_t fullPath[512];
    wchar_t snapshotFile[512];
} RegProtectedEntry;

#define MAX_REG_PROTECTED 128

// Partition protected list (disk -> snapshot of full partition table)
typedef struct {
    int diskNumber;
    wchar_t snapshotFile[512];
} PartProtectedEntry;

#define MAX_PART_PROTECTED 32

// ========== Global state ==========
static ProtectedEntry g_protectedList[MAX_PROTECTED_PROCESSES];
static int g_protectedCount = 0;

static SvcRepeatedEntry g_svcRepeated[MAX_SVC_REPEATED];
static int g_svcRepeatedCount = 0;

static RegRepeatedEntry g_regRepeated[MAX_REG_REPEATED];
static int g_regRepeatedCount = 0;

static PartRepeatedEntry g_partRepeated[MAX_PART_REPEATED];
static int g_partRepeatedCount = 0;

static RegProtectedEntry g_regProtected[MAX_REG_PROTECTED];
static int g_regProtectedCount = 0;

static PartProtectedEntry g_partProtected[MAX_PART_PROTECTED];
static int g_partProtectedCount = 0;

static FILE *g_logFile = NULL;
static CRITICAL_SECTION g_cs;

// ========== DLL function pointers ==========
// Service manager
typedef BOOL (*FnDeleteServiceByName)(const wchar_t *);
typedef BOOL (*FnStopServiceByName)(const wchar_t *);
static FnDeleteServiceByName svcDelete = NULL;
static FnStopServiceByName svcStop = NULL;

// Registry manager
typedef BOOL (*FnDeleteRegEntry)(HKEY, const wchar_t *, const wchar_t *);
typedef BOOL (*FnRegSetValueEx)(HKEY, const wchar_t *, const wchar_t *, DWORD, const BYTE *, DWORD);
typedef BOOL (*FnRegDeleteValueW)(HKEY, const wchar_t *, const wchar_t *);
typedef BOOL (*FnRegCreateKeyExW)(HKEY, const wchar_t *, HKEY *);
typedef int  (*FnRegGetValues)(HKEY, const wchar_t *, wchar_t ***, DWORD **, BYTE ***, DWORD **);
static FnDeleteRegEntry regDelete = NULL;
static FnRegSetValueEx regSetValue = NULL;
static FnRegCreateKeyExW regCreateKey = NULL;
static FnRegGetValues regGetValues = NULL;

// Partition manager
typedef BOOL (*FnDeletePartition)(int, int);
static FnDeletePartition partDelete = NULL;

// ========== Forward declarations ==========
void LoadAllConfigs(void);
void LoadSettings(void);
void Log(const wchar_t *format, ...);
DWORD GetProcessIDByName(const wchar_t *name);
BOOL KillProcessByName(const wchar_t *name, BOOL tree);
void LaunchProcess(const wchar_t *name);
void LoadCoreDlls(void);

// ========== Logger ==========
void Log(const wchar_t *format, ...) {
    if (!g_logFile) return;
    EnterCriticalSection(&g_cs);
    time_t now = time(NULL);
    wchar_t timeStr[64];
    wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
    fwprintf(g_logFile, L"[%s] ", timeStr);
    va_list args;
    va_start(args, format);
    vfwprintf(g_logFile, format, args);
    va_end(args);
    fwprintf(g_logFile, L"\n");
    fflush(g_logFile);
    LeaveCriticalSection(&g_cs);
}

// ========== Config loading ==========
void LoadProcessConfig(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Check if we're in a build\ subfolder
    int len = wcslen(exePath);
    if (len > 7) {
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    
    wchar_t configPath[MAX_PATH];
    wcscpy(configPath, exePath);
    wcscat(configPath, L"\\data\\config.dat");
    
    FILE *f = _wfopen(configPath, L"rb");
    if (!f) {
        Log(L"进程配置文件不存在: %s", configPath);
        g_protectedCount = 0;
        return;
    }
    fread(&g_protectedCount, sizeof(int), 1, f);
    if (g_protectedCount > MAX_PROTECTED_PROCESSES) g_protectedCount = MAX_PROTECTED_PROCESSES;
    fread(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
    
    Log(L"已加载 %d 个进程保护条目", g_protectedCount);
}

void LoadSvcRepeatedConfig(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Check if we're in a build\ subfolder
    int len = wcslen(exePath);
    if (len > 7) {
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    
    wchar_t configPath[MAX_PATH];
    wcscpy(configPath, exePath);
    wcscat(configPath, L"\\data\\svc_repeated.dat");
    
    FILE *f = _wfopen(configPath, L"rb");
    if (!f) {
        g_svcRepeatedCount = 0;
        return;
    }
    fread(&g_svcRepeatedCount, sizeof(int), 1, f);
    if (g_svcRepeatedCount > MAX_SVC_REPEATED) g_svcRepeatedCount = MAX_SVC_REPEATED;
    fread(g_svcRepeated, sizeof(SvcRepeatedEntry), g_svcRepeatedCount, f);
    fclose(f);
    
    Log(L"已加载 %d 个服务重复删除条目", g_svcRepeatedCount);
}

void LoadRegRepeatedConfig(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Check if we're in a build\ subfolder
    int len = wcslen(exePath);
    if (len > 7) {
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    
    wchar_t configPath[MAX_PATH];
    wcscpy(configPath, exePath);
    wcscat(configPath, L"\\data\\reg_repeated.dat");
    
    FILE *f = _wfopen(configPath, L"rb");
    if (!f) {
        g_regRepeatedCount = 0;
        return;
    }
    fread(&g_regRepeatedCount, sizeof(int), 1, f);
    if (g_regRepeatedCount > MAX_REG_REPEATED) g_regRepeatedCount = MAX_REG_REPEATED;
    fread(g_regRepeated, sizeof(RegRepeatedEntry), g_regRepeatedCount, f);
    fclose(f);
    
    Log(L"已加载 %d 个注册表重复删除条目", g_regRepeatedCount);
}

void LoadPartRepeatedConfig(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Check if we're in a build\ subfolder
    int len = wcslen(exePath);
    if (len > 7) {
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    
    wchar_t configPath[MAX_PATH];
    wcscpy(configPath, exePath);
    wcscat(configPath, L"\\data\\part_repeated.dat");
    
    FILE *f = _wfopen(configPath, L"rb");
    if (!f) {
        g_partRepeatedCount = 0;
        return;
    }
    fread(&g_partRepeatedCount, sizeof(int), 1, f);
    if (g_partRepeatedCount > MAX_PART_REPEATED) g_partRepeatedCount = MAX_PART_REPEATED;
    fread(g_partRepeated, sizeof(PartRepeatedEntry), g_partRepeatedCount, f);
    fclose(f);
    
    Log(L"已加载 %d 个分区重复删除条目", g_partRepeatedCount);
}

void LoadAllConfigs(void) {
    LoadProcessConfig();
    LoadSvcRepeatedConfig();
    LoadRegRepeatedConfig();
    LoadPartRepeatedConfig();
}

// Settings
static int g_checkInterval = 500;
static int g_reloadInterval = 30000;

void LoadSettings(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Check if we're in a build\ subfolder
    int len = wcslen(exePath);
    if (len > 7) {
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    
    wchar_t configPath[MAX_PATH];
    wcscpy(configPath, exePath);
    wcscat(configPath, L"\\data\\settings.ini");
    
    wchar_t checkStr[32] = L"";
    GetPrivateProfileStringW(L"Settings", L"CheckInterval", L"500", checkStr, 32, configPath);
    g_checkInterval = _wtoi(checkStr);
    if (g_checkInterval < 100) g_checkInterval = 100;
    if (g_checkInterval > 60000) g_checkInterval = 60000;
    
    wchar_t reloadStr[32] = L"";
    GetPrivateProfileStringW(L"Settings", L"ReloadInterval", L"30000", reloadStr, 32, configPath);
    g_reloadInterval = _wtoi(reloadStr);
    if (g_reloadInterval < 1000) g_reloadInterval = 1000;
    if (g_reloadInterval > 3600000) g_reloadInterval = 3600000;
    
    Log(L"已加载设置: 检查间隔=%dms, 重载间隔=%dms", g_checkInterval, g_reloadInterval);
}

// ========== Load Core DLLs ==========
void LoadCoreDlls(void) {
    wchar_t basePath[MAX_PATH];
    GetModuleFileNameW(NULL, basePath, MAX_PATH);
    wchar_t *p = wcsrchr(basePath, L'\\');
    if (p) *p = L'\0';
    
    // Check if we're in a build\ subfolder
    int len = wcslen(basePath);
    if (len > 7) {
        wchar_t *last = basePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    
    // service_manager.dll
    {
        wchar_t path[MAX_PATH];
        wcscpy(path, basePath);
        wcscat(path, L"\\service_manager.dll");
        HMODULE h = LoadLibraryW(path);
        if (h) {
            svcDelete = (FnDeleteServiceByName)GetProcAddress(h, "DeleteServiceByName");
            svcStop = (FnStopServiceByName)GetProcAddress(h, "StopServiceByName");
            Log(L"service_manager.dll 已加载");
        } else {
            Log(L"加载 service_manager.dll 失败: %lu", GetLastError());
        }
    }
    
    // registry_manager.dll
    {
        wchar_t path[MAX_PATH];
        wcscpy(path, basePath);
        wcscat(path, L"\\registry_manager.dll");
        HMODULE h = LoadLibraryW(path);
        if (h) {
            regDelete = (FnDeleteRegEntry)GetProcAddress(h, "DeleteRegistryEntry");
            Log(L"registry_manager.dll 已加载");
        } else {
            Log(L"加载 registry_manager.dll 失败: %lu", GetLastError());
        }
    }
    
    // partition_manager.dll
    {
        wchar_t path[MAX_PATH];
        wcscpy(path, basePath);
        wcscat(path, L"\\partition_manager.dll");
        HMODULE h = LoadLibraryW(path);
        if (h) {
            partDelete = (FnDeletePartition)GetProcAddress(h, "DeletePartition");
            Log(L"partition_manager.dll 已加载");
        } else {
            Log(L"加载 partition_manager.dll 失败: %lu", GetLastError());
        }
    }
}

// ========== Process operations ==========
DWORD GetProcessIDByName(const wchar_t *name) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    DWORD pid = 0;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

BOOL KillProcessByName(const wchar_t *name, BOOL tree) {
    DWORD pid = GetProcessIDByName(name);
    if (pid == 0) return FALSE;
    if (tree) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {0};
            pe.dwSize = sizeof(PROCESSENTRY32W);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ParentProcessID == pid) KillProcessByName(pe.szExeFile, TRUE);
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }
    // Enable SeDebugPrivilege
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid);
        AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, 0);
        CloseHandle(hToken);
    }
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) { TerminateProcess(hProc, 1); CloseHandle(hProc); return TRUE; }
    return FALSE;
}

// Launch a protected process - no console, fully detached
void LaunchProcess(const wchar_t *name) {
    wchar_t cmdLine[520];
    wcsncpy(cmdLine, name, 519);
    cmdLine[519] = L'\0';
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = INVALID_HANDLE_VALUE;
    si.hStdError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {0};
    
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    
    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        Log(L"已重启保护进程: %s", name);
    } else {
        Log(L"重启失败: %s (error=%lu)", name, GetLastError());
    }
}

// ========== Service operations ==========
void ExecuteSvcRepeatedDelete(void) {
    if (g_svcRepeatedCount == 0 || !svcDelete) return;
    
    for (int i = 0; i < g_svcRepeatedCount; i++) {
        BOOL ok = svcDelete(g_svcRepeated[i].name);
        if (ok) {
            Log(L"重复删除服务: %s", g_svcRepeated[i].name);
        }
    }
}

// ========== Registry operations ==========
void ExecuteRegRepeatedDelete(void) {
    if (g_regRepeatedCount == 0 || !regDelete) return;
    
    for (int i = 0; i < g_regRepeatedCount; i++) {
        HKEY hRoot = HKEY_LOCAL_MACHINE;
        const wchar_t *subKey = g_regRepeated[i].fullPath;
        
        if (wcsncmp(subKey, L"HKEY_LOCAL_MACHINE\\", 19) == 0) {
            hRoot = HKEY_LOCAL_MACHINE;
            subKey += 19;
        } else if (wcsncmp(subKey, L"HKEY_CURRENT_USER\\", 18) == 0) {
            hRoot = HKEY_CURRENT_USER;
            subKey += 18;
        } else if (wcsncmp(subKey, L"HKEY_CLASSES_ROOT\\", 18) == 0) {
            hRoot = HKEY_CLASSES_ROOT;
            subKey += 18;
        }
        
        BOOL ok = regDelete(hRoot, subKey, NULL);
        if (ok) {
            Log(L"重复删除注册表: %s", g_regRepeated[i].fullPath);
        }
    }
}

// ========== Partition operations ==========
void ExecutePartRepeatedDelete(void) {
    if (g_partRepeatedCount == 0 || !partDelete) return;

    for (int i = 0; i < g_partRepeatedCount; i++) {
        BOOL ok = partDelete(g_partRepeated[i].diskNumber, g_partRepeated[i].partitionNumber);
        if (ok) {
            Log(L"重复删除分区: disk=%d, part=%d", g_partRepeated[i].diskNumber, g_partRepeated[i].partitionNumber);
        }
    }
}

// ========== Registry Protection ==========
void LoadRegProtectedConfig(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    int len = wcslen(exePath);
    if (len > 7) {
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    wchar_t configPath[MAX_PATH];
    swprintf(configPath, MAX_PATH, L"%s\\data\\protected_reg.txt", exePath);

    g_regProtectedCount = 0;
    FILE *f = _wfopen(configPath, L"rt, ccs=UTF-8");
    if (!f) return;
    wchar_t line[1024];
    while (fgetws(line, 1024, f) && g_regProtectedCount < MAX_REG_PROTECTED) {
        wchar_t *nl = wcschr(line, L'\n'); if (nl) *nl = 0;
        wchar_t *cr = wcschr(line, L'\r'); if (cr) *cr = 0;
        if (line[0] == L'#' || line[0] == 0) continue;
        wcscpy(g_regProtected[g_regProtectedCount].fullPath, line);
        /* snapshot file path = same as registry_manager: %ProgramData%\ProcessGuardian\registry\<hash>.snapshot */
        /* Compute hash same way */
        DWORD h = 5381;
        const wchar_t *q = line;
        while (*q) { h = ((h << 5) + h) + (DWORD)towlower(*q); q++; }
        wchar_t basePath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, basePath))) {
            swprintf(g_regProtected[g_regProtectedCount].snapshotFile, 512,
                     L"%s\\ProcessGuardian\\registry\\%08x.snapshot", basePath, h);
        } else {
            swprintf(g_regProtected[g_regProtectedCount].snapshotFile, 512,
                     L"%s\\registry\\%08x.snapshot", exePath, h);
        }
        g_regProtectedCount++;
    }
    fclose(f);
    Log(L"已加载 %d 个注册表保护条目", g_regProtectedCount);
}

/* Check current state of protected reg key against snapshot.
   If different, restore from snapshot.
   Snapshot format (binary, see registry_manager.c SaveRegistrySnapshot):
     DWORD magic=0x52455350, ver=1
     WCHAR regPath[512] (with null)
     DWORD subkeyCount
     For each: WCHAR subname[256], DWORD subSubCount
       For each: WCHAR ssname[256], DWORD valueCount
         For each: WCHAR vname[256], DWORD type, DWORD size, BYTE data[...]
     DWORD numValues of root
     For each: WCHAR vname[256], DWORD type, DWORD size, BYTE data[...] */
static BOOL CompareRegKeyToSnapshot(HKEY hRoot, const wchar_t *subKey, FILE *sf) {
    /* Read snapshot header */
    DWORD magic, ver;
    fread(&magic, 4, 1, sf);
    fread(&ver, 4, 1, sf);
    if (magic != 0x52455350) return TRUE;  /* unknown = treat as different */
    wchar_t sPath[512];
    fread(sPath, 2, 512, sf);
    DWORD subCount;
    fread(&subCount, 4, 1, sf);

    HKEY hKey;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return TRUE;

    /* Compare subkey count */
    DWORD actualSubCount = 0;
    RegQueryInfoKeyW(hKey, NULL, NULL, NULL, &actualSubCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (actualSubCount != subCount) { RegCloseKey(hKey); return TRUE; }

    /* Compare each subkey existence */
    for (DWORD i = 0; i < subCount; i++) {
        wchar_t subName[256] = {0};
        fread(subName, 2, 512, sf);
        DWORD subSubCount;
        fread(&subSubCount, 4, 1, sf);
        HKEY hSub;
        if (RegOpenKeyExW(hRoot, subName, 0, KEY_READ, &hSub) != ERROR_SUCCESS) {
            RegCloseKey(hKey); return TRUE;
        }
        DWORD actualSubSub = 0;
        RegQueryInfoKeyW(hSub, NULL, NULL, NULL, &actualSubSub, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        if (actualSubSub != subSubCount) { RegCloseKey(hSub); RegCloseKey(hKey); return TRUE; }
        /* Compare values at this subkey */
        for (DWORD j = 0; j < subSubCount; j++) {
            wchar_t ssName[256] = {0};
            fread(ssName, 2, 512, sf);
            DWORD vCount;
            fread(&vCount, 4, 1, sf);
            HKEY hSS;
            if (RegOpenKeyExW(hSub, ssName, 0, KEY_READ, &hSS) != ERROR_SUCCESS) {
                RegCloseKey(hSub); RegCloseKey(hKey); return TRUE;
            }
            DWORD actualV = 0;
            RegQueryInfoKeyW(hSS, NULL, NULL, NULL, NULL, NULL, NULL, &actualV, NULL, NULL, NULL, NULL);
            if (actualV != vCount) { RegCloseKey(hSS); RegCloseKey(hSub); RegCloseKey(hKey); return TRUE; }
            /* For each value in snapshot, check it exists with same data */
            for (DWORD k = 0; k < vCount; k++) {
                wchar_t vName[256] = {0};
                fread(vName, 2, 512, sf);
                DWORD vType, vSize;
                fread(&vType, 4, 1, sf);
                fread(&vSize, 4, 1, sf);
                BYTE *vData = (BYTE*)malloc(vSize > 0 ? vSize : 1);
                if (vSize > 0) fread(vData, 1, vSize, sf);

                DWORD actualType, actualSize = 0;
                RegQueryValueExW(hSS, vName, NULL, &actualType, NULL, &actualSize);
                if (actualType != vType || actualSize != vSize) {
                    free(vData);
                    RegCloseKey(hSS); RegCloseKey(hSub); RegCloseKey(hKey);
                    return TRUE;
                }
                if (vSize > 0) {
                    BYTE *actualData = (BYTE*)malloc(vSize);
                    RegQueryValueExW(hSS, vName, NULL, NULL, actualData, &actualSize);
                    if (memcmp(actualData, vData, vSize) != 0) {
                        free(vData); free(actualData);
                        RegCloseKey(hSS); RegCloseKey(hSub); RegCloseKey(hKey);
                        return TRUE;
                    }
                    free(actualData);
                }
                free(vData);
            }
            RegCloseKey(hSS);
        }
        RegCloseKey(hSub);
    }
    RegCloseKey(hKey);
    return FALSE;  /* no difference */
}

/* Restore reg key from snapshot by deleting the key and recreating it */
static void RestoreRegKeyFromSnapshot(const wchar_t *fullPath) {
    Log(L"检测到注册表被修改，正在恢复: %s", fullPath);
    /* Walk through snapshot, recreate keys and set values */
    wchar_t snapFile[512];
    /* Find the entry */
    for (int i = 0; i < g_regProtectedCount; i++) {
        if (wcsicmp(g_regProtected[i].fullPath, fullPath) == 0) {
            wcscpy(snapFile, g_regProtected[i].snapshotFile);
            break;
        }
    }
    FILE *f = _wfopen(snapFile, L"rb");
    if (!f) { Log(L"无法打开快照: %s", snapFile); return; }

    HKEY hRoot = HKEY_LOCAL_MACHINE;
    wchar_t subKey[512] = {0};
    if (wcsncmp(fullPath, L"HKEY_LOCAL_MACHINE\\", 19) == 0) {
        hRoot = HKEY_LOCAL_MACHINE; wcscpy(subKey, fullPath + 19);
    } else if (wcsncmp(fullPath, L"HKEY_CURRENT_USER\\", 18) == 0) {
        hRoot = HKEY_CURRENT_USER; wcscpy(subKey, fullPath + 18);
    } else if (wcsncmp(fullPath, L"HKEY_CLASSES_ROOT\\", 18) == 0) {
        hRoot = HKEY_CLASSES_ROOT; wcscpy(subKey, fullPath + 18);
    } else if (wcsncmp(fullPath, L"HKEY_USERS\\", 11) == 0) {
        hRoot = HKEY_USERS; wcscpy(subKey, fullPath + 11);
    } else if (wcsncmp(fullPath, L"HKEY_CURRENT_CONFIG\\", 20) == 0) {
        hRoot = HKEY_CURRENT_CONFIG; wcscpy(subKey, fullPath + 20);
    } else {
        fclose(f); return;
    }

    /* Delete existing key (recursive) */
    wchar_t parentPath[512], keyName[256];
    wcscpy(parentPath, subKey);
    wchar_t *sl = wcsrchr(parentPath, L'\\');
    if (sl) {
        wcscpy(keyName, sl + 1); *sl = 0;
        HKEY hParent;
        if (RegOpenKeyExW(hRoot, parentPath, 0, DELETE | KEY_ENUMERATE_SUB_KEYS, &hParent) == ERROR_SUCCESS) {
            RegDeleteTreeW(hParent, keyName);
            RegCloseKey(hParent);
        }
    }

    /* Read snapshot */
    DWORD magic, ver;
    fread(&magic, 4, 1, f);
    fread(&ver, 4, 1, f);
    wchar_t sPath[512];
    fread(sPath, 2, 512, f);
    DWORD subCount;
    fread(&subCount, 4, 1, f);

    /* Recreate subkeys */
    for (DWORD i = 0; i < subCount; i++) {
        wchar_t subName[256] = {0};
        fread(subName, 2, 512, f);
        DWORD subSubCount;
        fread(&subSubCount, 4, 1, f);

        wchar_t fullSub[768];
        swprintf(fullSub, 768, L"%s\\%s", subKey, subName);
        HKEY hNew;
        if (RegCreateKeyExW(hRoot, fullSub, 0, NULL, 0, KEY_WRITE, NULL, &hNew, NULL) == ERROR_SUCCESS) {
            RegCloseKey(hNew);
        }
        for (DWORD j = 0; j < subSubCount; j++) {
            wchar_t ssName[256] = {0};
            fread(ssName, 2, 512, f);
            DWORD vCount;
            fread(&vCount, 4, 1, f);

            wchar_t fullSS[1024];
            swprintf(fullSS, 1024, L"%s\\%s\\%s", subKey, subName, ssName);
            HKEY hSSNew;
            if (RegCreateKeyExW(hRoot, fullSS, 0, NULL, 0, KEY_WRITE, NULL, &hSSNew, NULL) == ERROR_SUCCESS) {
                for (DWORD k = 0; k < vCount; k++) {
                    wchar_t vName[256] = {0};
                    fread(vName, 2, 512, f);
                    DWORD vType, vSize;
                    fread(&vType, 4, 1, f);
                    fread(&vSize, 4, 1, f);
                    BYTE *vData = (BYTE*)malloc(vSize > 0 ? vSize : 1);
                    if (vSize > 0) fread(vData, 1, vSize, f);
                    RegSetValueExW(hSSNew, vName, 0, vType, vData, vSize);
                    free(vData);
                }
                RegCloseKey(hSSNew);
            } else {
                /* skip values */
                for (DWORD k = 0; k < vCount; k++) {
                    wchar_t vName[256]; fread(vName, 2, 512, f);
                    DWORD vType, vSize; fread(&vType, 4, 1, f); fread(&vSize, 4, 1, f);
                    if (vSize > 0) { BYTE *t = (BYTE*)malloc(vSize); fread(t, 1, vSize, f); free(t); }
                }
            }
        }
    }
    fclose(f);
    Log(L"注册表已恢复: %s", fullPath);
}

void ExecuteRegProtection(void) {
    if (g_regProtectedCount == 0) return;
    for (int i = 0; i < g_regProtectedCount; i++) {
        const wchar_t *fullPath = g_regProtected[i].fullPath;
        HKEY hRoot = HKEY_LOCAL_MACHINE;
        wchar_t subKey[512] = {0};
        if (wcsncmp(fullPath, L"HKEY_LOCAL_MACHINE\\", 19) == 0) {
            hRoot = HKEY_LOCAL_MACHINE; wcscpy(subKey, fullPath + 19);
        } else if (wcsncmp(fullPath, L"HKEY_CURRENT_USER\\", 18) == 0) {
            hRoot = HKEY_CURRENT_USER; wcscpy(subKey, fullPath + 18);
        } else if (wcsncmp(fullPath, L"HKEY_CLASSES_ROOT\\", 18) == 0) {
            hRoot = HKEY_CLASSES_ROOT; wcscpy(subKey, fullPath + 18);
        } else { continue; }

        FILE *f = _wfopen(g_regProtected[i].snapshotFile, L"rb");
        if (!f) continue;
        if (CompareRegKeyToSnapshot(hRoot, subKey, f)) {
            fclose(f);
            RestoreRegKeyFromSnapshot(fullPath);
        } else {
            fclose(f);
        }
    }
}

// ========== Partition Protection ==========
void LoadPartProtectedConfig(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    int len = wcslen(exePath);
    if (len > 7) {
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';
        }
    }
    wchar_t configPath[MAX_PATH];
    swprintf(configPath, MAX_PATH, L"%s\\data\\protected_part.txt", exePath);

    g_partProtectedCount = 0;
    FILE *f = _wfopen(configPath, L"rt, ccs=UTF-8");
    if (!f) return;
    wchar_t line[1024];
    while (fgetws(line, 1024, f) && g_partProtectedCount < MAX_PART_PROTECTED) {
        wchar_t *nl = wcschr(line, L'\n'); if (nl) *nl = 0;
        wchar_t *cr = wcschr(line, L'\r'); if (cr) *cr = 0;
        if (line[0] == L'#' || line[0] == 0) continue;
        int disk = _wtoi(line);
        if (disk <= 0) continue;
        g_partProtected[g_partProtectedCount].diskNumber = disk;
        swprintf(g_partProtected[g_partProtectedCount].snapshotFile, 512,
                 L"%s\\partitions\\disk%d.snapshot", exePath, disk);
        g_partProtectedCount++;
    }
    fclose(f);
    Log(L"已加载 %d 个分区保护条目", g_partProtectedCount);
}

/* Read partition table and save to file (16 sectors = 8192 bytes from MBR/GPT) */
static BOOL ReadPartitionTable(int diskNumber, BYTE *buf, DWORD bufSize) {
    wchar_t path[64];
    swprintf(path, 64, L"\\\\.\\PhysicalDrive%d", diskNumber);
    HANDLE hDisk = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) return FALSE;
    DWORD bytesRead;
    BOOL ok = ReadFile(hDisk, buf, bufSize, &bytesRead, NULL);
    CloseHandle(hDisk);
    return ok && bytesRead == bufSize;
}

static void ExecutePartProtection(void) {
    if (g_partProtectedCount == 0) return;
    for (int i = 0; i < g_partProtectedCount; i++) {
        int disk = g_partProtected[i].diskNumber;
        /* Read current state */
        BYTE current[8192] = {0};
        if (!ReadPartitionTable(disk, current, 8192)) continue;
        /* Read snapshot */
        FILE *f = _wfopen(g_partProtected[i].snapshotFile, L"rb");
        if (!f) {
            /* First run: save current state as snapshot */
            f = _wfopen(g_partProtected[i].snapshotFile, L"wb");
            if (f) { fwrite(current, 1, 8192, f); fclose(f); }
            continue;
        }
        BYTE snapshot[8192] = {0};
        size_t bytesRead = fread(snapshot, 1, 8192, f);
        fclose(f);
        if (bytesRead != 8192) continue;
        if (memcmp(current, snapshot, 8192) != 0) {
            Log(L"检测到分区表被修改，正在恢复: disk=%d", disk);
            /* Restore by writing snapshot back to disk */
            wchar_t path[64];
            swprintf(path, 64, L"\\\\.\\PhysicalDrive%d", disk);
            HANDLE hDisk = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, 0, NULL);
            if (hDisk != INVALID_HANDLE_VALUE) {
                DWORD bytesWritten;
                BYTE zeroes[8192] = {0};
                WriteFile(hDisk, snapshot, 8192, &bytesWritten, NULL);
                CloseHandle(hDisk);
                Log(L"分区表已恢复: disk=%d", disk);
            } else {
                Log(L"无法打开磁盘写权限: disk=%d, err=%lu", disk, GetLastError());
            }
        }
    }
}

// ========== Main - Silent background daemon ==========
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // Check for single instance
    HANDLE hMutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;  // Already running
    }
    
    // Check for --hidden flag
    BOOL hiddenMode = FALSE;
    BOOL watchdogMode = FALSE;
    if (lpCmdLine) {
        while (*lpCmdLine == L' ' || *lpCmdLine == L'\t') lpCmdLine++;
        if (wcsncmp(lpCmdLine, L"--hidden", 8) == 0) {
            hiddenMode = TRUE;
        } else if (wcsncmp(lpCmdLine, L"--watchdog", 10) == 0) {
            watchdogMode = TRUE;
            hiddenMode = TRUE;
        } else if (lpCmdLine[0] == L'\0') {
            hiddenMode = TRUE;
        }
    }
    
    // ========== Watchdog mode ==========
    if (watchdogMode) {
        InitializeCriticalSection(&g_cs);
        wchar_t logPath[MAX_PATH];
        GetModuleFileNameW(NULL, logPath, MAX_PATH);
        wchar_t *p = wcsrchr(logPath, L'\\');
        if (p) *p = L'\0';
        wcscat(logPath, L"\\data\\process_guardian.log");
        g_logFile = _wfopen(logPath, L"a");
        Log(L"Guardian Watchdog started");
        
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(NULL, selfPath, MAX_PATH);
        
        while (TRUE) {
            BOOL found = FALSE;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = {0};
                pe.dwSize = sizeof(PROCESSENTRY32W);
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"guardiand.exe") == 0 &&
                            pe.th32ParentProcessID != GetCurrentProcessId()) {
                            found = TRUE;
                            break;
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }
            
            if (!found) {
                STARTUPINFOW si = {0};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi = {0};
                if (CreateProcessW(selfPath, L" --hidden", NULL, NULL, FALSE,
                                   CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                                   NULL, NULL, &si, &pi)) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    Log(L"Watchdog: 重启 guardiand.exe");
                }
            }
            
            Sleep(5000);
        }
    }
    
    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GuardianDaemon";
    RegisterClassExW(&wc);
    
    // Initialize
    InitializeCriticalSection(&g_cs);
    
    wchar_t logPath[MAX_PATH];
    GetModuleFileNameW(NULL, logPath, MAX_PATH);
    wchar_t *p = wcsrchr(logPath, L'\\');
    if (p) *p = L'\0';
    wcscat(logPath, L"\\data\\process_guardian.log");
    
    g_logFile = _wfopen(logPath, L"a");
    Log(L"Guardian Daemon started (hidden=%d)", hiddenMode);
    
    // Load core DLLs
    LoadCoreDlls();
    
    // Load all configs
    LoadAllConfigs();
    LoadRegProtectedConfig();
    LoadPartProtectedConfig();
    LoadSettings();
    
    // Main loop
    int tick = 0;
    int reloadTick = g_reloadInterval / g_checkInterval;
    while (TRUE) {
        tick++;
        if (tick >= 1) {
            EnterCriticalSection(&g_cs);
            
            // 1. Process protection & repeated kill
            for (int i = 0; i < g_protectedCount; i++) {
                DWORD pid = GetProcessIDByName(g_protectedList[i].name);
                if (g_protectedList[i].isRepeated) {
                    if (pid != 0) { 
                        KillProcessByName(g_protectedList[i].name, g_protectedList[i].isTree); 
                        Log(L"重复终止: %s", g_protectedList[i].name); 
                    }
                } else {
                    if (pid == 0) { 
                        LaunchProcess(g_protectedList[i].name); 
                        Log(L"保护进程重启: %s", g_protectedList[i].name); 
                    }
                }
            }
            
            // 2. Service repeated delete
            ExecuteSvcRepeatedDelete();
            
            // 3. Registry repeated delete
            ExecuteRegRepeatedDelete();
            
            // 4. Partition repeated delete
            ExecutePartRepeatedDelete();

            // 5. Registry protection (restore modified keys)
            ExecuteRegProtection();

            // 6. Partition protection (restore modified partition tables)
            ExecutePartProtection();

            LeaveCriticalSection(&g_cs);
            
            // Reload config periodically
            if (tick >= reloadTick) {
                LoadAllConfigs();
                LoadRegProtectedConfig();
                LoadPartProtectedConfig();
                LoadSettings();
                tick = 0;
                
                // Check for signal file
                wchar_t sigPath[MAX_PATH];
                GetModuleFileNameW(NULL, sigPath, MAX_PATH);
                p = wcsrchr(sigPath, L'\\');
                if (p) *p = L'\0';
                wcscat(sigPath, L"\\data\\reload.signal");
                if (_waccess(sigPath, 0) == 0) {
                    LoadAllConfigs();
                    _wremove(sigPath);
                    Log(L"配置已通过信号文件重新加载");
                }
            }
        }
        Sleep(g_checkInterval);
    }
}
