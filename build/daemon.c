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

// ========== Single instance mutex ==========
#define MUTEX_NAME L"Global\\GuardianDaemonMutex"

// ========== Data structures ==========
typedef struct {
    wchar_t name[260];
    DWORD pid;
    BOOL isTree;
    BOOL isRepeated;
} ProtectedEntry;

#define MAX_PROTECTED_PROCESSES 256

// ========== Global state ==========
static ProtectedEntry g_protectedList[MAX_PROTECTED_PROCESSES];
static int g_protectedCount = 0;
static FILE *g_logFile = NULL;
static CRITICAL_SECTION g_cs;

// ========== Forward declarations ==========
void LoadConfig(void);
void LoadSettings(void);
void Log(const wchar_t *format, ...);
DWORD GetProcessIDByName(const wchar_t *name);
BOOL KillProcessByName(const wchar_t *name, BOOL tree);
void LaunchProcess(const wchar_t *name);

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

// ========== Config ==========
void LoadConfig(void) {
    // Get the folder where this exe is located
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Check if we're in a build\ subfolder by looking for "build" in the path
    int len = wcslen(exePath);
    if (len > 7) {
        // Check last 6 chars for "build"
        wchar_t *last = exePath + len - 6;
        if (last[0] == L'\\' && last[1] == L'b' && last[2] == L'u' && last[3] == L'i' && last[4] == L'l' && last[5] == L'd') {
            *last = L'\0';  // Remove \build from path
        }
    }
    
    // Now build config path
    wchar_t configPath[MAX_PATH];
    wcscpy(configPath, exePath);
    wcscat(configPath, L"\\data\\config.dat");
    
    FILE *f = _wfopen(configPath, L"rb");
    if (!f) {
        Log(L"配置路径: %s", configPath);
        Log(L"配置不存在");
        g_protectedCount = 0;
        return;
    }
    fread(&g_protectedCount, sizeof(int), 1, f);
    fread(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
    
    Log(L"已加载 %d 个保护进程", g_protectedCount);
}

// Save settings (check interval)
static int g_checkInterval = 500;
static int g_reloadInterval = 30000;

void LoadSettings(void) {
    wchar_t configPath[MAX_PATH];
    GetModuleFileNameW(NULL, configPath, MAX_PATH);
    wchar_t *p = wcsrchr(configPath, L'\\');
    if (p) *p = L'\0';
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
    // Enable SeDebugPrivilege for admin-level termination
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        LookupPrivilegeValueW(NULL, L"SeDebugPrivilegeName", &tp.Privileges[0].Luid);
        AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, 0);
        CloseHandle(hToken);
    }
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) { TerminateProcess(hProc, 1); CloseHandle(hProc); return TRUE; }
    return FALSE;
}

// Launch a protected process - no console, fully detached
void LaunchProcess(const wchar_t *name) {
    // Expand name (could contain args)
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
    
    // CREATE_NO_WINDOW: 不创建控制台窗口
    // DETACHED_PROCESS: 进程从父进程的控制台分离
    // CREATE_NEW_PROCESS_GROUP: 防止 Ctrl+C 影响
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    
    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        Log(L"已重启保护进程: %s", name);
    } else {
        Log(L"重启失败: %s (error=%lu)", name, GetLastError());
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
    
    // Check for --hidden flag (set when launched by parent to suppress any window)
    BOOL hiddenMode = FALSE;
    BOOL watchdogMode = FALSE;  // --watchdog: monitor and restart main daemon
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
    
    // ========== Watchdog mode: keep restarting the actual daemon ==========
    if (watchdogMode) {
        InitializeCriticalSection(&g_cs);
        wchar_t logPath[MAX_PATH];
        GetModuleFileNameW(NULL, logPath, MAX_PATH);
        wchar_t *p = wcsrchr(logPath, L'\\');
        if (p) *p = L'\0';
        wcscat(logPath, L"\\data\\process_guardian.log");
        g_logFile = _wfopen(logPath, L"a");
        Log(L"Guardian Watchdog started");
        
        // Path of this executable
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(NULL, selfPath, MAX_PATH);
        
        // Main watchdog loop: every 5s check if a guardiand.exe with --hidden is running
        // If not, spawn it
        while (TRUE) {
            BOOL found = FALSE;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = {0};
                pe.dwSize = sizeof(PROCESSENTRY32W);
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        // Look for a guardiand.exe whose parent is NOT this watchdog
                        if (_wcsicmp(pe.szExeFile, L"guardiand.exe") == 0 &&
                            pe.th32ParentProcessID != GetCurrentProcessId()) {
                            // Check if it has --hidden in command line via WMI is overkill;
                            // just check the PPID isn't this watchdog's process.
                            // We use a marker file: if data\watchdog.marker doesn't exist,
                            // we consider this a daemon launched by us.
                            found = TRUE;
                            break;
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }
            
            if (!found) {
                // Spawn guardiand --hidden
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
    
    // Register window class (still useful for some Win32 features, harmless if unused)
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
    LoadConfig();
    LoadSettings();
    
    // Main loop - hiddenMode or normal mode both use the same loop (no message pump needed)
    int tick = 0;
    int reloadTick = g_reloadInterval / g_checkInterval;
    while (TRUE) {
        tick++;
        if (tick >= 1) {  // Check every interval
            EnterCriticalSection(&g_cs);
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
            LeaveCriticalSection(&g_cs);
            
            // Reload config every reloadInterval OR if signal file exists
            if (tick >= reloadTick) {
                LoadConfig();
                LoadSettings();
                tick = 0;
                
                wchar_t sigPath[MAX_PATH];
                GetModuleFileNameW(NULL, sigPath, MAX_PATH);
                p = wcsrchr(sigPath, L'\\');
                if (p) *p = L'\0';
                wcscat(sigPath, L"\\data\\reload.signal");
                if (_waccess(sigPath, 0) == 0) {
                    LoadConfig();
                    _wremove(sigPath);
                    Log(L"配置已重新加载");
                }
            }
        }
        Sleep(g_checkInterval);
    }
}
