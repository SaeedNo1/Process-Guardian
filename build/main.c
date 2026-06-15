#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdarg.h>

// Forward declarations
void InstallServiceAsSystem(HWND hwnd);

// ========== Internationalization ==========
typedef enum { LANG_ZH = 0, LANG_EN = 1 } AppLang;
static AppLang g_lang = LANG_ZH;

typedef struct {
    // Window titles
    const wchar_t *wndTitle;
    const wchar_t *settingsTitle;
    // Labels
    const wchar_t *labelRunning;
    const wchar_t *labelProtected;
    const wchar_t *hint;
    // Column headers
    const wchar_t *colName;
    const wchar_t *colPID;
    const wchar_t *colMemory;
    const wchar_t *colType;
    // Buttons
    const wchar_t *btnRefresh;
    const wchar_t *btnDaemon;
    const wchar_t *btnSettings;
    const wchar_t *btnLang;
    // Daemon menu
    const wchar_t *menuNormal;
    const wchar_t *menuAdmin;
    const wchar_t *menuSystem;
    // Running list context menu
    const wchar_t *menuKill;
    const wchar_t *menuKillTree;
    const wchar_t *menuRepKill;
    const wchar_t *menuRepKillTree;
    const wchar_t *menuProtect;
    const wchar_t *menuOpenLoc;
    const wchar_t *menuMonitor;
    const wchar_t *menuInstallSvc;
    // Protected list context menu
    const wchar_t *menuCancelRep;
    const wchar_t *menuCancelProt;
    // Type labels
    const wchar_t *typeRepKill;
    const wchar_t *typeRepKillTree;
    const wchar_t *typeProt;
    // Message boxes
    const wchar_t *msgSvcInstalled;
    const wchar_t *msgSvcInstalling;
    const wchar_t *msgNeedAdmin;
    const wchar_t *msgDaemonSystem;
    const wchar_t *msgSvcStartFail;
    const wchar_t *msgDaemonAdmin;
    const wchar_t *msgDaemonStarted;
    const wchar_t *msgSuccess;
    const wchar_t *msgInfo;
    const wchar_t *msgError;
    const wchar_t *msgSvcFail;
    const wchar_t *msgSvcFailDetail;
    const wchar_t *msgAutoStartSet;
    const wchar_t *msgWatchdogRunning;
} LangStrings;

static const LangStrings g_strings[2] = {
    // LANG_ZH
    {
        L"进程守护者",
        L"进程守护者 - 设置",
        L"正在运行的进程：",
        L"已保护的进程：",
        L"提示：右键进程可执行操作",
        L"名称", L"PID", L"内存(KB)", L"类型",
        L"刷新列表", L"启动守护", L"设置", L"EN",
        L"普通权限", L"管理员权限", L"SYSTEM权限",
        L"结束进程", L"结束进程树",
        L"重复结束进程", L"重复结束进程树",
        L"保护进程", L"打开进程位置", L"实时监控", L"安装SYSTEM服务",
        L"取消重复终止", L"取消保护",
        L"重复结束", L"重复结束(树)", L"保护",
        L"服务已安装并启动！", L"服务安装成功，尝试启动...",
        L"需要管理员权限来安装服务！",
        L"守护进程已以 SYSTEM 权限启动！",
        L"启动服务失败！",
        L"守护进程已以管理员权限启动！",
        L"守护进程已启动并设置开机自启",
        L"成功", L"提示", L"错误",
        L"服务启动失败！\n请以管理员身份运行主程序。",
        L"服务启动失败！\n请以管理员身份运行主程序。",
        L"已通过任务计划程序设置开机自启（无需等待桌面）",
        L"已启动 watchdog 守护（即使主守护被关闭也会自动恢复）",
    },
    // LANG_EN
    {
        L"Process Guardian",
        L"Process Guardian - Settings",
        L"Running Processes:",
        L"Protected Processes:",
        L"Tip: Right-click a process for options",
        L"Name", L"PID", L"Memory(KB)", L"Type",
        L"Refresh", L"Start Daemon", L"Settings", L"中文",
        L"Normal", L"Administrator", L"SYSTEM",
        L"Terminate Process", L"Terminate Tree",
        L"Repeated Terminate", L"Repeated Terminate Tree",
        L"Protect Process", L"Open Location", L"Live Monitor", L"Install SYSTEM Service",
        L"Cancel Repeated Terminate", L"Cancel Protection",
        L"Rep. Terminate", L"Rep. Terminate (Tree)", L"Protected",
        L"Service installed and started!", L"Service installed, starting...",
        L"Administrator privileges required to install service!",
        L"Daemon started with SYSTEM privileges!",
        L"Failed to start service!",
        L"Daemon started with Administrator privileges!",
        L"Daemon started and set to auto-run",
        L"Success", L"Info", L"Error",
        L"Failed to start service!\nPlease run the main program as Administrator.",
        L"Failed to start service!\nPlease run the main program as Administrator.",
        L"Auto-start registered via Task Scheduler (no wait for desktop)",
        L"Watchdog started (the daemon will auto-restart if killed)",
    }
};

#define STR(key) (g_strings[g_lang].key)

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
void InitLogger(void);
void Log(const wchar_t *format, ...);
void CloseLogger(void);
void LoadConfig(void);
void SaveConfig(void);
void AddToRepeatedList(const wchar_t *name);
void AddToRepeatedTreeList(const wchar_t *name);
void AddToProtectedList(const wchar_t *name);
void RemoveFromList(int index);
int FindInList(const wchar_t *name);
int FindInListByPID(DWORD pid);
DWORD GetProcessIDByName(const wchar_t *name);
BOOL KillProcessByPID(DWORD pid);
BOOL KillProcessTree(DWORD pid);
void RefreshRunningList(HWND hwnd);
void RefreshProtectedList(HWND hwnd);
void ShowSettingsDialog(HWND hwnd);
void ApplyLanguage(HWND hwnd);
void LoadLangSetting(void);
void SaveLangSetting(void);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void GetGuardianExe(wchar_t *out, int outLen);
static void GetWrapperExe(wchar_t *out, int outLen);
static void GetInstallExe(wchar_t *out, int outLen);
static BOOL SpawnGuardDaemon(BOOL admin);
static void SetAutoStart(BOOL adminMode);
static BOOL IsRunAsAdmin(void);

// ========== Logger ==========
void InitLogger(void) {
    InitializeCriticalSection(&g_cs);
    wchar_t logPath[MAX_PATH];
    GetModuleFileNameW(NULL, logPath, MAX_PATH);
    wchar_t *p = wcsrchr(logPath, L'\\');
    if (p) *p = L'\0';
    wcscat(logPath, L"\\data\\process_guardian.log");
    
    g_logFile = _wfopen(logPath, L"a");
    if (g_logFile) {
        time_t now = time(NULL);
        wchar_t timeStr[64];
        wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
        fwprintf(g_logFile, L"[%s] Guardian started\n", timeStr);
        fflush(g_logFile);
    }
}

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

void CloseLogger(void) {
    if (g_logFile) {
        time_t now = time(NULL);
        wchar_t timeStr[64];
        wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
        fwprintf(g_logFile, L"[%s] Guardian stopped\n", timeStr);
        fclose(g_logFile);
        g_logFile = NULL;
    }
    DeleteCriticalSection(&g_cs);
}

// ========== Config ==========
void LoadConfig(void) {
    wchar_t configPath[MAX_PATH];
    GetModuleFileNameW(NULL, configPath, MAX_PATH);
    wchar_t *p = wcsrchr(configPath, L'\\');
    if (p) *p = L'\0';
    wcscat(configPath, L"\\data\\config.dat");
    
    FILE *f = _wfopen(configPath, L"rb");
    if (!f) return;
    fread(&g_protectedCount, sizeof(int), 1, f);
    fread(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
}

void SaveConfig(void) {
    wchar_t configPath[MAX_PATH];
    GetModuleFileNameW(NULL, configPath, MAX_PATH);
    wchar_t *p = wcsrchr(configPath, L'\\');
    if (p) *p = L'\0';
    wcscat(configPath, L"\\data\\config.dat");
    
    FILE *f = _wfopen(configPath, L"wb");
    if (!f) return;
    fwrite(&g_protectedCount, sizeof(int), 1, f);
    fwrite(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
}

// ========== Language persistence ==========
static wchar_t g_settingsPath[MAX_PATH];

void GetSettingsPath(void) {
    if (g_settingsPath[0] != L'\0') return;
    GetModuleFileNameW(NULL, g_settingsPath, MAX_PATH);
    wchar_t *p = wcsrchr(g_settingsPath, L'\\');
    if (p) *p = L'\0';
    wcscat(g_settingsPath, L"\\data\\settings.ini");
}

void LoadLangSetting(void) {
    GetSettingsPath();
    wchar_t val[16] = L"";
    GetPrivateProfileStringW(L"UI", L"Language", L"zh", val, 16, g_settingsPath);
    g_lang = (_wcsicmp(val, L"en") == 0) ? LANG_EN : LANG_ZH;
}

void SaveLangSetting(void) {
    GetSettingsPath();
    WritePrivateProfileStringW(L"UI", L"Language", (g_lang == LANG_EN) ? L"en" : L"zh", g_settingsPath);
}

// ========== List management ==========
void AddToRepeatedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED_PROCESSES && FindInList(name) < 0) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 259);
        g_protectedList[g_protectedCount].name[259] = L'\0';
        g_protectedList[g_protectedCount].pid = GetProcessIDByName(name);
        g_protectedList[g_protectedCount].isTree = FALSE;
        g_protectedList[g_protectedCount].isRepeated = TRUE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void AddToRepeatedTreeList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED_PROCESSES && FindInList(name) < 0) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 259);
        g_protectedList[g_protectedCount].name[259] = L'\0';
        g_protectedList[g_protectedCount].pid = GetProcessIDByName(name);
        g_protectedList[g_protectedCount].isTree = TRUE;
        g_protectedList[g_protectedCount].isRepeated = TRUE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void AddToProtectedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED_PROCESSES && FindInList(name) < 0) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 259);
        g_protectedList[g_protectedCount].name[259] = L'\0';
        g_protectedList[g_protectedCount].pid = GetProcessIDByName(name);
        g_protectedList[g_protectedCount].isTree = FALSE;
        g_protectedList[g_protectedCount].isRepeated = FALSE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void RemoveFromList(int index) {
    EnterCriticalSection(&g_cs);
    if (index >= 0 && index < g_protectedCount) {
        for (int j = index; j < g_protectedCount - 1; j++) {
            g_protectedList[j] = g_protectedList[j + 1];
        }
        g_protectedCount--;
        SaveConfig();
        
        // Create reload signal file
        wchar_t sigPath[MAX_PATH];
        GetModuleFileNameW(NULL, sigPath, MAX_PATH);
        wchar_t *p = wcsrchr(sigPath, L'\\');
        if (p) *p = L'\0';
        wcscat(sigPath, L"\\data\\reload.signal");
        FILE *f = _wfopen(sigPath, L"w");
        if (f) fclose(f);
    }
    LeaveCriticalSection(&g_cs);
}

int FindInList(const wchar_t *name) {
    for (int i = 0; i < g_protectedCount; i++) {
        if (_wcsicmp(g_protectedList[i].name, name) == 0) return i;
    }
    return -1;
}

int FindInListByPID(DWORD pid) {
    for (int i = 0; i < g_protectedCount; i++) {
        if (g_protectedList[i].pid == pid) return i;
    }
    return -1;
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

BOOL KillProcessByPID(DWORD pid) {
    if (pid == 0 || pid == 4) return FALSE;
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) {
        // Try SeDebugPrivilege
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            TOKEN_PRIVILEGES tp;
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            LookupPrivilegeValueW(NULL, L"SeDebugPrivilegeName", &tp.Privileges[0].Luid);
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, 0);
            CloseHandle(hToken);
        }
        hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    }
    if (!hProc) return FALSE;
    BOOL result = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    return result;
}

BOOL KillProcessTree(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {0};
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid) KillProcessTree(pe.th32ProcessID);
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return KillProcessByPID(pid);
}

// ========== Main ==========
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    
    // Silent operational modes (no UI). Run as windowed subsystem but no main window.
    if (lpCmdLine) {
        while (*lpCmdLine == L' ' || *lpCmdLine == L'\t') lpCmdLine++;
        // --install-system: silently install and start the SYSTEM service, then exit
        if (wcsncmp(lpCmdLine, L"--install-system", 16) == 0) {
            CreateDirectoryW(L"data", NULL);
            wchar_t installExe[MAX_PATH];
            GetInstallExe(installExe, MAX_PATH);
            STARTUPINFOW si = {0};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {0};
            if (CreateProcessW(installExe, L" install", NULL, NULL, FALSE,
                               CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 20000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            // Signal the waiting parent window
            HANDLE hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\GuardianInstallDone");
            if (hEvt) { SetEvent(hEvt); CloseHandle(hEvt); }
            return 0;
        }
        // --hidden-launch: silently start admin daemon + watchdog, then exit
        if (wcsncmp(lpCmdLine, L"--hidden-launch", 15) == 0) {
            CreateDirectoryW(L"data", NULL);
            // We are now admin (runas succeeded). Start the daemon and watchdog.
            if (SpawnGuardDaemon(TRUE)) {
                // Watchdog
                wchar_t exePath[MAX_PATH];
                GetGuardianExe(exePath, MAX_PATH);
                STARTUPINFOW si2 = {0};
                si2.cb = sizeof(si2);
                si2.dwFlags = STARTF_USESHOWWINDOW;
                si2.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi2 = {0};
                if (CreateProcessW(exePath, L" --watchdog", NULL, NULL, FALSE,
                                   CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                                   NULL, NULL, &si2, &pi2)) {
                    CloseHandle(pi2.hProcess);
                    CloseHandle(pi2.hThread);
                }
                SetAutoStart(TRUE);
            }
            return 0;
        }
        // --watchdog: just keep the daemon alive (handled in guardiand.exe normally,
        // but if invoked as guardian.exe the same logic could be added; here we just exit
        // because the canonical watchdog is guardiand.exe)
        if (wcsncmp(lpCmdLine, L"--watchdog", 10) == 0) {
            return 0;
        }
    }
    
    CreateDirectoryW(L"data", NULL);
    InitLogger();
    InitializeCriticalSection(&g_cs);
    LoadConfig();
    LoadLangSetting();

    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GuardianApp";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hMainWindow = CreateWindowExW(0, L"GuardianApp", STR(wndTitle),
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                900, 550, NULL, NULL, hInstance, NULL);

    if (!hMainWindow) return 1;

    ShowWindow(hMainWindow, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseLogger();
    return (int)msg.wParam;
}

// ========== GUI ==========
static HWND hRunningList;
static HWND hProtectedList;
static HWND hLabelRunning;
static HWND hLabelProtected;
static HWND hLabelHint;
static HWND hBtnRefresh;
static HWND hBtnDaemon;
static HWND hBtnSettings;
static HWND hBtnLang;
static HFONT g_hFont;

// Get path to guardiand.exe
static void GetGuardianExe(wchar_t *out, int outLen) {
    GetModuleFileNameW(NULL, out, outLen);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = L'\0';
    wcscat(out, L"\\guardiand.exe");
}

static void GetWrapperExe(wchar_t *out, int outLen) {
    GetModuleFileNameW(NULL, out, outLen);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = L'\0';
    wcscat(out, L"\\service_wrapper.exe");
}

static void GetInstallExe(wchar_t *out, int outLen) {
    GetModuleFileNameW(NULL, out, outLen);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = L'\0';
    wcscat(out, L"\\install_service.exe");
}

// Spawn guardiand.exe in a fully hidden, no-console window
static BOOL SpawnGuardDaemon(BOOL admin) {
    wchar_t exePath[MAX_PATH];
    GetGuardianExe(exePath, MAX_PATH);
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    
    // CREATE_NO_WINDOW: 无控制台窗口
    // DETACHED_PROCESS: 进程从父进程控制台分离
    // CREATE_NEW_PROCESS_GROUP: 新进程组，防止 Ctrl+C 传播
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    
    BOOL ok = CreateProcessW(exePath, L" --hidden", NULL, NULL, FALSE, flags,
                              NULL, NULL, &si, &pi);
    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok;
}

// Set auto-start via Task Scheduler (works without waiting for Explorer)
// Falls back to HKCU\Run if Task Scheduler fails
static void SetAutoStart(BOOL adminMode) {
    wchar_t exePath[MAX_PATH];
    GetGuardianExe(exePath, MAX_PATH);
    
    // Build schtasks command
    wchar_t cmd[2048];
    swprintf(cmd, 2048,
        L"schtasks /Create /TN \"GuardianDaemon\" /TR \"\\\"%s\\\" --hidden\" /SC ONSTART /RL HIGHEST /F",
        exePath);
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = INVALID_HANDLE_VALUE;
    si.hStdError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {0};
    
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    // Fallback: also write HKCU\Run so legacy auto-start works
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t regValue[MAX_PATH + 20];
        swprintf(regValue, MAX_PATH + 20, L"\"%s\" --hidden", exePath);
        RegSetValueExW(hKey, L"GuardianDaemon", 0, REG_SZ,
                       (const BYTE*)regValue,
                       (wcslen(regValue) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

static BOOL IsRunAsAdmin(void) {
    BOOL f = FALSE;
    PSID p = NULL;
    SID_IDENTIFIER_AUTHORITY a = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&a, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &p)) {
        CheckTokenMembership(NULL, p, &f);
        FreeSid(p);
    }
    return f;
}

// 0=普通, 1=管理员, 2=系统
void StartDaemon(HWND hwnd, int mode) {
    wchar_t exePath[MAX_PATH];
    GetGuardianExe(exePath, MAX_PATH);
    
    // SYSTEM mode - use service
    if (mode == 2) {
        wchar_t installExe[MAX_PATH];
        GetInstallExe(installExe, MAX_PATH);
        
        // Check if service already exists
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        BOOL serviceExists = FALSE;
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_QUERY_STATUS);
            if (hSvc) {
                serviceExists = TRUE;
                CloseServiceHandle(hSvc);
            }
            CloseServiceHandle(hSCM);
        }
        
        if (!IsRunAsAdmin()) {
            // Need admin to install/control the service. Re-launch this exe with runas.
            wchar_t guardianExe[MAX_PATH];
            GetModuleFileNameW(NULL, guardianExe, MAX_PATH);
            wchar_t params[1024];
            swprintf(params, 1024, L"--install-system");

            // Create a named event to wait for elevated child to finish
            HANDLE hEvt = CreateEventW(NULL, FALSE, FALSE, L"Global\\GuardianInstallDone");

            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"runas";
            sei.lpFile = guardianExe;
            sei.lpParameters = params;
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei)) {
                // Wait for the elevated child to signal completion (up to 30s)
                if (hwnd && hEvt) {
                    DWORD waitR = WaitForSingleObject(hEvt, 30000);
                    CloseHandle(hEvt);
                    // Verify service is now actually running
                    BOOL running = FALSE;
                    SC_HANDLE hSCM2 = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
                    if (hSCM2) {
                        SC_HANDLE hSvc2 = OpenServiceW(hSCM2, L"GuardianDaemon", SERVICE_QUERY_STATUS);
                        if (hSvc2) {
                            SERVICE_STATUS st2;
                            if (QueryServiceStatus(hSvc2, &st2) && st2.dwCurrentState == SERVICE_RUNNING)
                                running = TRUE;
                            CloseServiceHandle(hSvc2);
                        }
                        CloseServiceHandle(hSCM2);
                    }
                    if (running) {
                        MessageBoxW(hwnd, STR(msgDaemonSystem), STR(msgSuccess), MB_OK);
                    } else {
                        MessageBoxW(hwnd, STR(msgSvcStartFail), STR(msgError), MB_OK);
                    }
                } else if (hEvt) {
                    CloseHandle(hEvt);
                }
                return;
            } else {
                if (hEvt) CloseHandle(hEvt);
                if (hwnd) MessageBoxW(hwnd, STR(msgNeedAdmin), STR(msgError), MB_OK);
                return;
            }
        }
        
        // We're admin. Install service if missing, then start it.
        if (!serviceExists) {
            // Launch install_service.exe install with our own admin token
            STARTUPINFOW si = {0};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {0};
            if (CreateProcessW(installExe, L" install", NULL, NULL, FALSE,
                               CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 15000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
        
        // Start (or restart) the service
        hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
            if (hSvc) {
                SERVICE_STATUS st;
                if (QueryServiceStatus(hSvc, &st) && st.dwCurrentState == SERVICE_RUNNING) {
                    ControlService(hSvc, SERVICE_CONTROL_STOP, &st);
                    // Wait for actual stop
                    int wait = 0;
                    while (wait < 20) {
                        if (QueryServiceStatus(hSvc, &st) && st.dwCurrentState == SERVICE_STOPPED) break;
                        Sleep(250);
                        wait++;
                    }
                }
                if (StartServiceW(hSvc, 0, NULL)) {
                    Log(L"Daemon started (SYSTEM service)");
                    if (hwnd) MessageBoxW(hwnd, STR(msgDaemonSystem), STR(msgSuccess), MB_OK);
                } else {
                    DWORD err = GetLastError();
                    if (err == ERROR_SERVICE_ALREADY_RUNNING) {
                        if (hwnd) MessageBoxW(hwnd, STR(msgDaemonSystem), STR(msgSuccess), MB_OK);
                    } else if (err == 2 /*ERROR_FILE_NOT_FOUND*/ || err == 1058 /*ERROR_SERVICE_DISABLED*/ || err == 1060 /*ERROR_SERVICE_DOES_NOT_EXIST*/) {
                        // Service binary missing/wrong path, disabled, or missing - auto-reinstall
                        CloseServiceHandle(hSvc);
                        CloseServiceHandle(hSCM);
                        wchar_t installExe[MAX_PATH];
                        GetInstallExe(installExe, MAX_PATH);
                        STARTUPINFOW siR = {0};
                        siR.cb = sizeof(siR);
                        siR.dwFlags = STARTF_USESHOWWINDOW;
                        siR.wShowWindow = SW_HIDE;
                        PROCESS_INFORMATION piR = {0};
                        if (CreateProcessW(installExe, L" install", NULL, NULL, FALSE,
                                           CREATE_NO_WINDOW, NULL, NULL, &siR, &piR)) {
                            WaitForSingleObject(piR.hProcess, 20000);
                            CloseHandle(piR.hProcess);
                            CloseHandle(piR.hThread);
                            if (hwnd) MessageBoxW(hwnd, STR(msgDaemonSystem), STR(msgSuccess), MB_OK);
                        } else {
                            wchar_t errMsg2[256];
                            swprintf(errMsg2, 256, L"%s\nError: %lu", STR(msgSvcStartFail), err);
                            if (hwnd) MessageBoxW(hwnd, errMsg2, STR(msgError), MB_OK);
                        }
                        return;
                    } else {
                        wchar_t errMsg[256];
                        swprintf(errMsg, 256, L"%s\nError: %lu", STR(msgSvcStartFail), err);
                        if (hwnd) MessageBoxW(hwnd, errMsg, STR(msgError), MB_OK);
                    }
                }
                CloseServiceHandle(hSvc);
                CloseServiceHandle(hSCM);
                return;
            }
            CloseServiceHandle(hSCM);
        }
        if (hwnd) MessageBoxW(hwnd, STR(msgSvcStartFail), STR(msgError), MB_OK);
        return;
    }
    
    // Admin mode (mode == 1) - elevated but not SYSTEM
    if (mode == 1) {
        if (!IsRunAsAdmin()) {
            // Re-launch with UAC elevation
            wchar_t guardianExe[MAX_PATH];
            GetModuleFileNameW(NULL, guardianExe, MAX_PATH);
            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"runas";
            sei.lpParameters = L"--hidden-launch";
            sei.lpFile = guardianExe;
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei)) {
                if (hwnd) MessageBoxW(hwnd, STR(msgDaemonAdmin), STR(msgInfo), MB_OK);
                return;
            } else {
                if (hwnd) MessageBoxW(hwnd, STR(msgNeedAdmin), STR(msgError), MB_OK);
                return;
            }
        }
        
        // Already admin: spawn guardiand --hidden, then start watchdog
        if (SpawnGuardDaemon(TRUE)) {
            Log(L"Daemon started (Admin)");
            
            // Start watchdog (in --watchdog mode) to keep the daemon alive
            STARTUPINFOW si = {0};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {0};
            wchar_t cmdLine[MAX_PATH + 20];
            swprintf(cmdLine, MAX_PATH + 20, L"\"%s\" --watchdog", exePath);
            // cmdLine is the module path itself, since guardiand == guardian_exe in this code base
            // (daemon logic is in guardiand.exe - same binary)
            if (CreateProcessW(exePath, L" --watchdog", NULL, NULL, FALSE,
                               CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                               NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                Log(L"Watchdog started");
            }
            
            // Register auto-start
            SetAutoStart(TRUE);
            Log(L"Registered auto-start (Task Scheduler + HKCU\\Run)");
            
            if (hwnd) {
                wchar_t info[512];
                swprintf(info, 512, L"%s\n\n%s", STR(msgDaemonAdmin), STR(msgWatchdogRunning));
                MessageBoxW(hwnd, info, STR(msgInfo), MB_OK);
            }
            return;
        }
    }
    
    // Normal launch without admin (mode == 0)
    if (SpawnGuardDaemon(FALSE)) {
        Log(L"Daemon started (Normal)");
        SetAutoStart(FALSE);
        if (hwnd) MessageBoxW(hwnd, STR(msgDaemonStarted), STR(msgInfo), MB_OK);
    }
}

// Update all UI text labels and column headers according to current language
void ApplyLanguage(HWND hwnd) {
    // Window title
    SetWindowTextW(hwnd, STR(wndTitle));

    // Static labels
    SetWindowTextW(hLabelRunning, STR(labelRunning));
    SetWindowTextW(hLabelProtected, STR(labelProtected));
    SetWindowTextW(hLabelHint, STR(hint));

    // Buttons
    SetWindowTextW(hBtnRefresh, STR(btnRefresh));
    SetWindowTextW(hBtnDaemon, STR(btnDaemon));
    SetWindowTextW(hBtnSettings, STR(btnSettings));
    SetWindowTextW(hBtnLang, STR(btnLang));

    // Running list columns
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT;
    col.pszText = (wchar_t*)STR(colName);
    ListView_SetColumn(hRunningList, 0, &col);
    col.pszText = (wchar_t*)STR(colPID);
    ListView_SetColumn(hRunningList, 1, &col);
    col.pszText = (wchar_t*)STR(colMemory);
    ListView_SetColumn(hRunningList, 2, &col);

    // Protected list columns
    col.pszText = (wchar_t*)STR(colName);
    ListView_SetColumn(hProtectedList, 0, &col);
    col.pszText = (wchar_t*)STR(colPID);
    ListView_SetColumn(hProtectedList, 1, &col);
    col.pszText = (wchar_t*)STR(colType);
    ListView_SetColumn(hProtectedList, 2, &col);

    // Refresh data so type labels also update
    RefreshRunningList(hwnd);
    RefreshProtectedList(hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    
    switch (msg) {
        case WM_CREATE: {
            g_hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

            // Left panel label
            hLabelRunning = CreateWindowW(L"static", STR(labelRunning),
                       WS_CHILD | WS_VISIBLE,
                       10, 10, 220, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLabelRunning, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            hRunningList = CreateWindowExW(0, L"SysListView32", L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT,
                                    10, 35, 420, 420, hwnd,
                                    (HMENU)1001, GetModuleHandle(NULL), NULL);
            SendMessageW(hRunningList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            LVCOLUMNW col = {0};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = (wchar_t*)STR(colName); col.cx = 180;
            ListView_InsertColumn(hRunningList, 0, &col);
            col.pszText = (wchar_t*)STR(colPID); col.cx = 70;
            ListView_InsertColumn(hRunningList, 1, &col);
            col.pszText = (wchar_t*)STR(colMemory); col.cx = 80;
            ListView_InsertColumn(hRunningList, 2, &col);
            ListView_SetExtendedListViewStyle(hRunningList, LVS_EX_FULLROWSELECT | LVS_EX_MULTIWORKAREAS);

            // Right panel label
            hLabelProtected = CreateWindowW(L"static", STR(labelProtected),
                       WS_CHILD | WS_VISIBLE,
                       460, 10, 220, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLabelProtected, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            hProtectedList = CreateWindowExW(0, L"SysListView32", L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT,
                                    460, 35, 420, 420, hwnd,
                                    (HMENU)1002, GetModuleHandle(NULL), NULL);
            SendMessageW(hProtectedList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            col.pszText = (wchar_t*)STR(colName); col.cx = 180;
            ListView_InsertColumn(hProtectedList, 0, &col);
            col.pszText = (wchar_t*)STR(colPID); col.cx = 70;
            ListView_InsertColumn(hProtectedList, 1, &col);
            col.pszText = (wchar_t*)STR(colType); col.cx = 100;
            ListView_InsertColumn(hProtectedList, 2, &col);
            ListView_SetExtendedListViewStyle(hProtectedList, LVS_EX_FULLROWSELECT);

            // Buttons row
            hBtnRefresh = CreateWindowW(L"button", STR(btnRefresh),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       10, 465, 90, 28, hwnd,
                                       (HMENU)2001, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnRefresh, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            hBtnDaemon = CreateWindowW(L"button", STR(btnDaemon),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                       110, 465, 110, 28, hwnd,
                                       (HMENU)2005, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnDaemon, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            hBtnSettings = CreateWindowW(L"button", STR(btnSettings),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       230, 465, 70, 28, hwnd,
                                       (HMENU)2004, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnSettings, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            // Language toggle button
            hBtnLang = CreateWindowW(L"button", STR(btnLang),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       310, 465, 50, 28, hwnd,
                                       (HMENU)2006, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnLang, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            // Hint label
            hLabelHint = CreateWindowW(L"static", STR(hint),
                       WS_CHILD | WS_VISIBLE, 460, 468, 380, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLabelHint, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            RefreshRunningList(hwnd);
            RefreshProtectedList(hwnd);
            
            // Auto-start daemon with admin rights
            StartDaemon(NULL, 1);
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 2001) {  // Refresh
                RefreshRunningList(hwnd);
                RefreshProtectedList(hwnd);
            } else if (LOWORD(wParam) == 2004) {  // Settings
                ShowSettingsDialog(hwnd);
            } else if (LOWORD(wParam) == 2005) {  // Start daemon - show menu
                RECT rc;
                GetWindowRect((HWND)lParam, &rc);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, STR(menuNormal));
                AppendMenuW(hMenu, MF_STRING, 2, STR(menuAdmin));
                AppendMenuW(hMenu, MF_STRING, 3, STR(menuSystem));
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, rc.left, rc.bottom, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                if (cmd == 1) StartDaemon(hwnd, 0);
                else if (cmd == 2) StartDaemon(hwnd, 1);
                else if (cmd == 3) StartDaemon(hwnd, 2);
            } else if (LOWORD(wParam) == 2006) {  // Language toggle
                g_lang = (g_lang == LANG_ZH) ? LANG_EN : LANG_ZH;
                SaveLangSetting();
                ApplyLanguage(hwnd);
            }
            break;
        case WM_CONTEXTMENU: {
            int x = (int)(short)LOWORD(lParam);
            int y = (int)(short)HIWORD(lParam);
            POINT pt = {x, y};
            ScreenToClient(hwnd, &pt);
            
            // Check if clicked on running list
            if (ChildWindowFromPoint(hwnd, pt) == hRunningList) {
                // Get all selected items
                int selCount = 0;
                int selItems[100];
                int sel = ListView_GetNextItem(hRunningList, -1, LVNI_SELECTED);
                while (sel >= 0 && selCount < 100) {
                    selItems[selCount++] = sel;
                    sel = ListView_GetNextItem(hRunningList, sel, LVNI_SELECTED);
                }
                if (selCount == 0) break;
                
                // Get names of all selected processes
                wchar_t names[100][260];
                for (int i = 0; i < selCount; i++) {
                    LVITEMW lvi = {0};
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = selItems[i];
                    lvi.pszText = names[i];
                    lvi.cchTextMax = 260;
                    ListView_GetItem(hRunningList, &lvi);
                }
                
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, STR(menuKill));
                AppendMenuW(hMenu, MF_STRING, 2, STR(menuKillTree));
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 3, STR(menuRepKill));
                AppendMenuW(hMenu, MF_STRING, 4, STR(menuRepKillTree));
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 5, STR(menuProtect));
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 6, STR(menuOpenLoc));
                AppendMenuW(hMenu, MF_STRING, 7, STR(menuMonitor));
                AppendMenuW(hMenu, MF_STRING, 8, STR(menuInstallSvc));
                
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                
                // Process all selected items
                for (int i = 0; i < selCount; i++) {
                    wchar_t *name = names[i];
                    DWORD pid = GetProcessIDByName(name);
                    switch (cmd) {
                        case 1: KillProcessByPID(pid); Log(L"Kill process: %s", name); break;
                        case 2: KillProcessTree(pid); Log(L"Kill process tree: %s", name); break;
                        case 3: AddToRepeatedList(name); Log(L"Add repeated terminate: %s", name); break;
                        case 4: AddToRepeatedTreeList(name); Log(L"Add repeated terminate tree: %s", name); break;
                        case 5: AddToProtectedList(name); Log(L"Protect process: %s", name); break;
                        case 6: // Open location
                        {
                            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                            if (hSnap != INVALID_HANDLE_VALUE) {
                                PROCESSENTRY32W pe = {0};
                                pe.dwSize = sizeof(PROCESSENTRY32W);
                                if (Process32FirstW(hSnap, &pe)) {
                                    do {
                                        if (_wcsicmp(pe.szExeFile, name) == 0) {
                                            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                                            if (hProc) {
                                                wchar_t exePath2[MAX_PATH], dirPath[MAX_PATH];
                                                if (GetModuleFileNameExW(hProc, NULL, exePath2, MAX_PATH)) {
                                                    wcscpy(dirPath, exePath2);
                                                    wchar_t *pp = wcsrchr(dirPath, L'\\');
                                                    if (pp) *pp = L'\0';
                                                    ShellExecuteW(NULL, L"explore", dirPath, NULL, NULL, SW_SHOW);
                                                }
                                                CloseHandle(hProc);
                                            }
                                            break;
                                        }
                                    } while (Process32NextW(hSnap, &pe));
                                }
                                CloseHandle(hSnap);
                            }
                        }
                        break;
                    case 7: // Real-time monitor
                        {
                            wchar_t exePath2[MAX_PATH], cmdLine[MAX_PATH + 50];
                            GetModuleFileNameW(NULL, exePath2, MAX_PATH);
                            wchar_t *pp = wcsrchr(exePath2, L'\\');
                            if (pp) *pp = L'\0';
                            wcscat(exePath2, L"\\observer.exe");
                            swprintf(cmdLine, MAX_PATH + 50, L"\"%s\" --pid %lu", exePath2, pid);
                            STARTUPINFOW si = {0};
                            PROCESS_INFORMATION piInfo = {0};
                            si.cb = sizeof(si);
                            CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &piInfo);
                            CloseHandle(piInfo.hProcess);
                            CloseHandle(piInfo.hThread);
                        }
                        break;
                    case 8: // Install SYSTEM service
                        InstallServiceAsSystem(hwnd);
                        break;
                    }
                }
                RefreshRunningList(hwnd);
                RefreshProtectedList(hwnd);
                DestroyMenu(hMenu);
            }
            // Check if clicked on protected list
            else if (ChildWindowFromPoint(hwnd, pt) == hProtectedList) {
                int sel = ListView_GetNextItem(hProtectedList, -1, LVNI_SELECTED);
                if (sel == -1) break;

                // Save name before removal
                wchar_t removedName[260];
                if (sel >= 0 && sel < g_protectedCount)
                    wcsncpy(removedName, g_protectedList[sel].name, 259);
                else
                    removedName[0] = L'\0';
                
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 11, STR(menuCancelRep));
                AppendMenuW(hMenu, MF_STRING, 12, STR(menuCancelProt));
                
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                
                if (cmd == 11 || cmd == 12) {
                    Log(L"Remove from list: %s", removedName);
                    RemoveFromList(sel);
                }
                RefreshProtectedList(hwnd);
                DestroyMenu(hMenu);
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void RefreshRunningList(HWND hwnd) {
    (void)hwnd;
    ListView_DeleteAllItems(hRunningList);
    
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;
    
    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    
    if (!Process32FirstW(hSnap, &pe)) {
        CloseHandle(hSnap);
        return;
    }
    
    int i = 0;
    do {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = pe.szExeFile;
        ListView_InsertItem(hRunningList, &lvi);
        
        wchar_t pidStr[32];
        swprintf(pidStr, 32, L"%lu", (unsigned long)pe.th32ProcessID);
        lvi.iSubItem = 1;
        lvi.pszText = pidStr;
        ListView_SetItem(hRunningList, &lvi);
        
        // Get memory
        DWORD memKB = 0;
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                memKB = pmc.WorkingSetSize / 1024;
            }
            CloseHandle(hProc);
        }
        wchar_t memStr[32];
        swprintf(memStr, 32, L"%lu", (unsigned long)memKB);
        lvi.iSubItem = 2;
        lvi.pszText = memStr;
        ListView_SetItem(hRunningList, &lvi);
        
        i++;
    } while (Process32NextW(hSnap, &pe));
    
    CloseHandle(hSnap);
}

void RefreshProtectedList(HWND hwnd) {
    (void)hwnd;
    ListView_DeleteAllItems(hProtectedList);
    
    for (int i = 0; i < g_protectedCount; i++) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = g_protectedList[i].name;
        ListView_InsertItem(hProtectedList, &lvi);
        
        wchar_t pidStr[32];
        DWORD pid = g_protectedList[i].pid;
        if (pid == 0) pid = GetProcessIDByName(g_protectedList[i].name);
        swprintf(pidStr, 32, L"%lu", (unsigned long)pid);
        lvi.iSubItem = 1;
        lvi.pszText = pidStr;
        ListView_SetItem(hProtectedList, &lvi);
        
        lvi.iSubItem = 2;
        if (g_protectedList[i].isRepeated) {
            lvi.pszText = g_protectedList[i].isTree ? (wchar_t*)STR(typeRepKillTree) : (wchar_t*)STR(typeRepKill);
        } else {
            lvi.pszText = (wchar_t*)STR(typeProt);
        }
        ListView_SetItem(hProtectedList, &lvi);
    }
}

// Settings dialog - simple input
void ShowSettingsDialog(HWND hwnd) {
    // Launch settings.exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    wcscat(exePath, L"\\settings.exe");
    
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    
    CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

// Install and start service as SYSTEM
void InstallServiceAsSystem(HWND hwnd) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Delete old service
    {
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", DELETE);
            if (hSvc) {
                DeleteService(hSvc);
                CloseServiceHandle(hSvc);
            }
            CloseServiceHandle(hSCM);
        }
    }
    
    // Create new service
    wcscat(exePath, L"\\service_wrapper.exe");
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM) {
        SC_HANDLE hSvc = CreateServiceW(
            hSCM, L"GuardianDaemon", L"Guardian Daemon",
            SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            exePath, NULL, NULL, NULL, NULL, NULL);
        if (hSvc) {
            CloseServiceHandle(hSvc);
            
            // Start service
            hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
            if (hSvc) {
                StartServiceW(hSvc, 0, NULL);
                CloseServiceHandle(hSvc);
                MessageBoxW(hwnd, STR(msgSvcInstalled), STR(msgSuccess), MB_OK);
                CloseServiceHandle(hSCM);
                return;
            }
        }
        CloseServiceHandle(hSCM);
    }
    
    MessageBoxW(hwnd, STR(msgSvcFail), STR(msgError), MB_OK);
}
