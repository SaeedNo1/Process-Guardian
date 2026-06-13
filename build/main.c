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
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
    
    CreateDirectoryW(L"data", NULL);
    InitLogger();
    InitializeCriticalSection(&g_cs);
    LoadConfig();

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GuardianApp";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hMainWindow = CreateWindowExW(0, L"GuardianApp", L"进程守护者",
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

// 0=普通, 1=管理员, 2=系统
void StartDaemon(HWND hwnd, int mode) {
    // Get path to guardiand.exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    wcscat(exePath, L"\\guardiand.exe");
    
    // SYSTEM mode - use service
    if (mode == 2) {
        // Check if service exists
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
        
        if (!serviceExists) {
            // Try to install service - need admin, request via elevation
            wchar_t wrapperPath[MAX_PATH];
            GetModuleFileNameW(NULL, wrapperPath, MAX_PATH);
            wchar_t *p = wcsrchr(wrapperPath, L'\\');
            if (p) *p = L'\0';
            wcscat(wrapperPath, L"\\service_wrapper.exe");
            
            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"runas";
            wchar_t cmd[MAX_PATH + 20];
            swprintf(cmd, MAX_PATH + 20, L"/c sc create GuardianDaemon binPath= \"\"%s\"\" start= auto", wrapperPath);
            sei.lpFile = L"cmd.exe";
            sei.lpParameters = cmd;
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei)) {
                Sleep(1000);
                // Try to start service
                hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
                if (hSCM) {
                    SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
                    if (hSvc) {
                        StartServiceW(hSvc, 0, NULL);
                        CloseServiceHandle(hSvc);
                        CloseServiceHandle(hSCM);
                        MessageBoxW(hwnd, L"服务已安装并启动！", L"成功", MB_OK);
                        return;
                    }
                    CloseServiceHandle(hSCM);
                }
                MessageBoxW(hwnd, L"服务安装成功，尝试启动...", L"提示", MB_OK);
                return;
            } else {
                MessageBoxW(hwnd, L"需要管理员权限来安装服务！", L"错误", MB_OK);
                return;
            }
        }
        
        // Service exists - start it
        hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
            if (hSvc) {
                SERVICE_STATUS st;
                if (QueryServiceStatus(hSvc, &st) && st.dwCurrentState == SERVICE_RUNNING) {
                    ControlService(hSvc, SERVICE_CONTROL_STOP, &st);
                    Sleep(500);
                }
                StartServiceW(hSvc, 0, NULL);
                CloseServiceHandle(hSvc);
                CloseServiceHandle(hSCM);
                Log(L"守护进程已启动(SYSTEM)");
                MessageBoxW(hwnd, L"守护进程已以 SYSTEM 权限启动！", L"成功", MB_OK);
                return;
            }
            CloseServiceHandle(hSCM);
        }
        MessageBoxW(hwnd, L"启动服务失败！", L"错误", MB_OK);
        return;
    }
    
    // Admin mode
    if (mode == 1) {
        // Request admin privileges via ShellExecute but with a workaround for hidden window
        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        wchar_t cmdLine[MAX_PATH + 20];
        wcscpy(cmdLine, exePath);
        wcscat(cmdLine, L" --hidden");
        sei.lpParameters = cmdLine;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.nShow = SW_HIDE;
        
        if (ShellExecuteExW(&sei)) {
            Log(L"守护进程已启动(管理员)");
            
            // Register auto-start with hidden flag
            HKEY hKey;
            wcscpy(cmdLine, exePath);
            wcscat(cmdLine, L" --hidden");
            
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegSetValueExW(hKey, L"GuardianDaemon", 0, REG_SZ, (const BYTE*)cmdLine, (wcslen(cmdLine) + 1) * sizeof(wchar_t));
                RegCloseKey(hKey);
                Log(L"已注册开机自启");
            }
            
            if (hwnd) MessageBoxW(hwnd, L"守护进程已以管理员权限启动！", L"提示", MB_OK);
            return;
        }
    }
    
    // Normal launch without admin
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        Log(L"守护进程已启动");
    }
    
    // Register auto-start
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"GuardianDaemon", 0, REG_SZ, (const BYTE*)exePath, (wcslen(exePath) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
        Log(L"已注册开机自启");
    }
    
    if (hwnd) {
        MessageBoxW(hwnd, L"守护进程已启动并设置开机自启", L"提示", MB_OK);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFont;
    (void)lParam;
    
    switch (msg) {
        case WM_CREATE: {
            hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

            // Left panel - running processes
            CreateWindowW(L"static", L"正在运行的进程：",
                       WS_CHILD | WS_VISIBLE,
                       10, 10, 200, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);

            hRunningList = CreateWindowExW(0, L"SysListView32", L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT,
                                    10, 35, 420, 420, hwnd,
                                    (HMENU)1001, GetModuleHandle(NULL), NULL);
            SendMessageW(hRunningList, WM_SETFONT, (WPARAM)hFont, TRUE);

            LVCOLUMNW col = {0};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = L"名称"; col.cx = 180;
            ListView_InsertColumn(hRunningList, 0, &col);
            col.pszText = L"PID"; col.cx = 70;
            ListView_InsertColumn(hRunningList, 1, &col);
            col.pszText = L"内存(KB)"; col.cx = 80;
            ListView_InsertColumn(hRunningList, 2, &col);
            ListView_SetExtendedListViewStyle(hRunningList, LVS_EX_FULLROWSELECT | LVS_EX_MULTIWORKAREAS);

            // Right panel - protected list
            CreateWindowW(L"static", L"已保护的进程：",
                       WS_CHILD | WS_VISIBLE,
                       460, 10, 200, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);

            hProtectedList = CreateWindowExW(0, L"SysListView32", L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT,
                                    460, 35, 420, 420, hwnd,
                                    (HMENU)1002, GetModuleHandle(NULL), NULL);
            SendMessageW(hProtectedList, WM_SETFONT, (WPARAM)hFont, TRUE);

            col.pszText = L"名称"; col.cx = 180;
            ListView_InsertColumn(hProtectedList, 0, &col);
            col.pszText = L"PID"; col.cx = 70;
            ListView_InsertColumn(hProtectedList, 1, &col);
            col.pszText = L"类型"; col.cx = 100;
            ListView_InsertColumn(hProtectedList, 2, &col);
            ListView_SetExtendedListViewStyle(hProtectedList, LVS_EX_FULLROWSELECT);

            // Refresh button
            HWND hRefreshBtn = CreateWindowW(L"button", L"刷新列表",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       10, 465, 80, 28, hwnd,
                                       (HMENU)2001, GetModuleHandle(NULL), NULL);
            SendMessageW(hRefreshBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Start button with dropdown (SYSTEM selection)
            CreateWindowW(L"button", L"启动守护",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                       100, 465, 100, 28, hwnd,
                                       (HMENU)2005, GetModuleHandle(NULL), NULL);

            // Settings button
            HWND hSetBtn = CreateWindowW(L"button", L"设置",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       210, 465, 80, 28, hwnd,
                                       (HMENU)2004, GetModuleHandle(NULL), NULL);
            SendMessageW(hSetBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Daemon info
            CreateWindowW(L"static", L"提示：右键进程可执行操作",
                       WS_CHILD, 460, 465, 300, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);

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
                GetWindowRect((HWND)wParam, &rc);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, L"普通权限");
                AppendMenuW(hMenu, MF_STRING, 2, L"管理员权限");
                AppendMenuW(hMenu, MF_STRING, 3, L"SYSTEM权限");
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, rc.left, rc.top, 0, hwnd, NULL);
                if (cmd == 1) StartDaemon(hwnd, 0);      // Normal
                else if (cmd == 2) StartDaemon(hwnd, 1);  // Admin
                else if (cmd == 3) StartDaemon(hwnd, 2);  // SYSTEM
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
                AppendMenuW(hMenu, MF_STRING, 1, L"结束进程");
                AppendMenuW(hMenu, MF_STRING, 2, L"结束进程树");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, L"");
                AppendMenuW(hMenu, MF_STRING, 3, L"重复结束进程");
                AppendMenuW(hMenu, MF_STRING, 4, L"重复结束进程树");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, L"");
                AppendMenuW(hMenu, MF_STRING, 5, L"保护进程");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, L"");
                AppendMenuW(hMenu, MF_STRING, 6, L"打开进程位置");
                AppendMenuW(hMenu, MF_STRING, 7, L"实时监控");
                AppendMenuW(hMenu, MF_STRING, 8, L"安装SYSTEM服务");
                
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                
                // Process all selected items
                for (int i = 0; i < selCount; i++) {
                    wchar_t *name = names[i];
                    DWORD pid = GetProcessIDByName(name);
                    switch (cmd) {
                        case 1: KillProcessByPID(pid); Log(L"结束进程: %s", name); break;
                        case 2: KillProcessTree(pid); Log(L"结束进程树: %s", name); break;
                        case 3: AddToRepeatedList(name); Log(L"添加重复结束: %s", name); break;
                        case 4: AddToRepeatedTreeList(name); Log(L"添加重复结束进程树: %s", name); break;
                        case 5: AddToProtectedList(name); Log(L"保护进程: %s", name); break;
                        case 6: // Open location
                        {
                            wchar_t dirPath[MAX_PATH];
                            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                            if (hSnap != INVALID_HANDLE_VALUE) {
                                PROCESSENTRY32W pe = {0};
                                pe.dwSize = sizeof(PROCESSENTRY32W);
                                if (Process32FirstW(hSnap, &pe)) {
                                    do {
                                        if (_wcsicmp(pe.szExeFile, name) == 0) {
                                            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                                            if (hProc) {
                                                wchar_t exePath[MAX_PATH];
                                                if (GetModuleFileNameW((HMODULE)hProc, exePath, MAX_PATH)) {
                                                    wcscpy(dirPath, exePath);
                                                    wchar_t *p = wcsrchr(dirPath, L'\\');
                                                    if (p) *p = L'\0';
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
                            wchar_t exePath[MAX_PATH], cmdLine[MAX_PATH + 50];
                            GetModuleFileNameW(NULL, exePath, MAX_PATH);
                            wchar_t *p = wcsrchr(exePath, L'\\');
                            if (p) *p = L'\0';
                            wcscat(exePath, L"\\observer.exe");
                            swprintf(cmdLine, MAX_PATH + 50, L"\"%s\" --pid %lu", exePath, pid);
                            STARTUPINFOW si = {0};
                            PROCESS_INFORMATION pi = {0};
                            si.cb = sizeof(si);
                            CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);
                            CloseHandle(pi.hProcess);
                            CloseHandle(pi.hThread);
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
                
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 11, L"取消重复终止");
                AppendMenuW(hMenu, MF_STRING, 12, L"取消保护");
                
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                
                if (cmd == 11 || cmd == 12) {
                    RemoveFromList(sel);
                    Log(L"从列表删除: %s", g_protectedList[sel].name);
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
            lvi.pszText = g_protectedList[i].isTree ? L"重复结束(树)" : L"重复结束";
        } else {
            lvi.pszText = L"保护";
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
                MessageBoxW(hwnd, L"服务已安装并以 SYSTEM 身份启动！", L"成功", MB_OK);
                CloseServiceHandle(hSCM);
                return;
            }
        }
        CloseServiceHandle(hSCM);
    }
    
    MessageBoxW(hwnd, L"服务启动失败！\n请以管理员身份运行主程序。", L"错误", MB_OK);
}