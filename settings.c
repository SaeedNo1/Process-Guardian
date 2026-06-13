#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Settings file path
#define SETTINGS_FILE L"data\\settings.ini"

// Default values
#define DEFAULT_CHECK_INTERVAL 500    // ms between checks
#define DEFAULT_RELOAD_INTERVAL 30000 // ms between config reloads

// Global controls
static HWND hCheckInterval;
static HWND hReloadInterval;
static HWND hDialog;

// Load settings
void LoadSettings(int *checkInterval, int *reloadInterval) {
    *checkInterval = DEFAULT_CHECK_INTERVAL;
    *reloadInterval = DEFAULT_RELOAD_INTERVAL;
    
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\" SETTINGS_FILE);
    
    // Read check interval
    wchar_t checkStr[32] = L"";
    GetPrivateProfileStringW(L"Settings", L"CheckInterval", L"", checkStr, 32, path);
    if (checkStr[0] != L'\0') {
        *checkInterval = _wtoi(checkStr);
    }
    
    // Read reload interval
    wchar_t reloadStr[32] = L"";
    GetPrivateProfileStringW(L"Settings", L"ReloadInterval", L"", reloadStr, 32, path);
    if (reloadStr[0] != L'\0') {
        *reloadInterval = _wtoi(reloadStr);
    }
}

// Save settings
void SaveSettings(int checkInterval, int reloadInterval) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\" SETTINGS_FILE);
    
    // Ensure directory exists
    wchar_t dirPath[MAX_PATH];
    wcscpy(dirPath, path);
    p = wcsrchr(dirPath, L'\\');
    if (p) *p = L'\0';
    CreateDirectoryW(dirPath, NULL);
    
    // Write check interval
    wchar_t checkStr[32];
    swprintf(checkStr, 32, L"%d", checkInterval);
    WritePrivateProfileStringW(L"Settings", L"CheckInterval", checkStr, path);
    
    // Write reload interval
    wchar_t reloadStr[32];
    swprintf(reloadStr, 32, L"%d", reloadInterval);
    WritePrivateProfileStringW(L"Settings", L"ReloadInterval", reloadStr, path);
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                // Get values
                wchar_t checkStr[32], reloadStr[32];
                GetWindowTextW(hCheckInterval, checkStr, 32);
                GetWindowTextW(hReloadInterval, reloadStr, 32);
                
                int checkInterval = _wtoi(checkStr);
                int reloadInterval = _wtoi(reloadStr);
                
                // Validate
                if (checkInterval < 100) checkInterval = 100;
                if (checkInterval > 60000) checkInterval = 60000;
                if (reloadInterval < 1000) reloadInterval = 1000;
                if (reloadInterval > 3600000) reloadInterval = 3600000;
                
                // Save
                SaveSettings(checkInterval, reloadInterval);
                
                MessageBoxW(hwnd, L"设置已保存！\n守护进程将在下次检查时自动加载新设置。", L"提示", MB_OK);
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            break;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int main(void) {
    HINSTANCE hInstance = GetModuleHandleW(NULL);
    
    // Create directory if needed
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data");
    CreateDirectoryW(path, NULL);
    
    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GuardianSettings";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);
    
    // Create dialog window
    hDialog = CreateWindowExW(0, L"GuardianSettings", L"进程守护者 - 设置",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 230,
        NULL, NULL, hInstance, NULL);
    
    if (!hDialog) return 1;
    
    // Load current settings
    int checkInterval, reloadInterval;
    LoadSettings(&checkInterval, &reloadInterval);
    
    // Labels and inputs
    CreateWindowW(L"STATIC", L"检查间隔 (毫秒, 100-60000):",
        WS_CHILD | WS_VISIBLE, 20, 20, 250, 20, hDialog, NULL, hInstance, NULL);
    
    wchar_t checkStr[32];
    swprintf(checkStr, 32, L"%d", checkInterval);
    hCheckInterval = CreateWindowW(L"EDIT", checkStr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        20, 45, 100, 25, hDialog, (HMENU)1001, hInstance, NULL);
    
    CreateWindowW(L"STATIC", L"重载配置间隔 (毫秒, 1000-3600000):",
        WS_CHILD | WS_VISIBLE, 20, 85, 280, 20, hDialog, NULL, hInstance, NULL);
    
    wchar_t reloadStr[32];
    swprintf(reloadStr, 32, L"%d", reloadInterval);
    hReloadInterval = CreateWindowW(L"EDIT", reloadStr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        20, 110, 100, 25, hDialog, (HMENU)1002, hInstance, NULL);
    
    CreateWindowW(L"BUTTON", L"保存",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        70, 160, 70, 28, hDialog, (HMENU)IDOK, hInstance, NULL);
    
    CreateWindowW(L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        160, 160, 70, 28, hDialog, (HMENU)IDCANCEL, hInstance, NULL);
    
    ShowWindow(hDialog, SW_SHOW);
    
    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return 0;
}