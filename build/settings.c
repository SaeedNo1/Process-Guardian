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

// ========== Language ==========
typedef enum { LANG_ZH = 0, LANG_EN = 1 } AppLang;
static AppLang g_lang = LANG_ZH;

typedef struct {
    const wchar_t *wndTitle;
    const wchar_t *labelCheck;
    const wchar_t *labelReload;
    const wchar_t *btnSave;
    const wchar_t *btnCancel;
    const wchar_t *btnLang;
    const wchar_t *msgSaved;
    const wchar_t *msgSavedTitle;
} SettingsLang;

static const SettingsLang g_sl[2] = {
    // ZH
    {
        L"进程守护者 - 设置",
        L"检查间隔 (毫秒, 100-60000):",
        L"重载配置间隔 (毫秒, 1000-3600000):",
        L"保存", L"取消", L"EN",
        L"设置已保存！\n守护进程将在下次检查时自动加载新设置。",
        L"提示",
    },
    // EN
    {
        L"Process Guardian - Settings",
        L"Check Interval (ms, 100-60000):",
        L"Reload Interval (ms, 1000-3600000):",
        L"Save", L"Cancel", L"中文",
        L"Settings saved!\nThe daemon will auto-reload on next check.",
        L"Info",
    }
};

#define SL(key) (g_sl[g_lang].key)

// Global controls
static HWND hCheckInterval;
static HWND hReloadInterval;
static HWND hDialog;
static HWND hLabelCheck;
static HWND hLabelReload;
static HWND hBtnSave;
static HWND hBtnCancel;
static HWND hBtnLang;

// Settings file full path
static wchar_t g_settingsPath[MAX_PATH];

void GetSettingsPath(void) {
    if (g_settingsPath[0] != L'\0') return;
    GetModuleFileNameW(NULL, g_settingsPath, MAX_PATH);
    wchar_t *p = wcsrchr(g_settingsPath, L'\\');
    if (p) *p = L'\0';
    // Build path: exe_dir\data\settings.ini
    wchar_t tmp[MAX_PATH];
    wcscpy(tmp, g_settingsPath);
    wcscat(tmp, L"\\");
    wcscat(tmp, SETTINGS_FILE);
    wcscpy(g_settingsPath, tmp);
}

// Load settings
void LoadSettings(int *checkInterval, int *reloadInterval) {
    *checkInterval = DEFAULT_CHECK_INTERVAL;
    *reloadInterval = DEFAULT_RELOAD_INTERVAL;
    
    GetSettingsPath();
    
    wchar_t checkStr[32] = L"";
    GetPrivateProfileStringW(L"Settings", L"CheckInterval", L"", checkStr, 32, g_settingsPath);
    if (checkStr[0] != L'\0') *checkInterval = _wtoi(checkStr);
    
    wchar_t reloadStr[32] = L"";
    GetPrivateProfileStringW(L"Settings", L"ReloadInterval", L"", reloadStr, 32, g_settingsPath);
    if (reloadStr[0] != L'\0') *reloadInterval = _wtoi(reloadStr);
}

// Save settings
void SaveSettings(int checkInterval, int reloadInterval) {
    GetSettingsPath();
    
    // Ensure directory exists
    wchar_t dirPath[MAX_PATH];
    wcscpy(dirPath, g_settingsPath);
    wchar_t *p = wcsrchr(dirPath, L'\\');
    if (p) *p = L'\0';
    CreateDirectoryW(dirPath, NULL);
    
    wchar_t checkStr[32];
    swprintf(checkStr, 32, L"%d", checkInterval);
    WritePrivateProfileStringW(L"Settings", L"CheckInterval", checkStr, g_settingsPath);
    
    wchar_t reloadStr[32];
    swprintf(reloadStr, 32, L"%d", reloadInterval);
    WritePrivateProfileStringW(L"Settings", L"ReloadInterval", reloadStr, g_settingsPath);
}

// Load/Save language preference
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

// Apply language to settings window
void ApplyLang(void) {
    SetWindowTextW(hDialog, SL(wndTitle));
    SetWindowTextW(hLabelCheck, SL(labelCheck));
    SetWindowTextW(hLabelReload, SL(labelReload));
    SetWindowTextW(hBtnSave, SL(btnSave));
    SetWindowTextW(hBtnCancel, SL(btnCancel));
    SetWindowTextW(hBtnLang, SL(btnLang));
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
                
                MessageBoxW(hwnd, SL(msgSaved), SL(msgSavedTitle), MB_OK);
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == 2010) {  // Language toggle
                g_lang = (g_lang == LANG_ZH) ? LANG_EN : LANG_ZH;
                SaveLangSetting();
                ApplyLang();
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
    
    // Ensure data dir
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data");
    CreateDirectoryW(path, NULL);
    
    // Load language preference first
    LoadLangSetting();
    
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
    hDialog = CreateWindowExW(0, L"GuardianSettings", SL(wndTitle),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 340, 250,
        NULL, NULL, hInstance, NULL);
    
    if (!hDialog) return 1;
    
    HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    
    // Load current settings
    int checkInterval, reloadInterval;
    LoadSettings(&checkInterval, &reloadInterval);
    
    // Labels and inputs
    hLabelCheck = CreateWindowW(L"STATIC", SL(labelCheck),
        WS_CHILD | WS_VISIBLE, 20, 20, 280, 20, hDialog, NULL, hInstance, NULL);
    SendMessageW(hLabelCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    wchar_t checkStr[32];
    swprintf(checkStr, 32, L"%d", checkInterval);
    hCheckInterval = CreateWindowW(L"EDIT", checkStr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        20, 45, 100, 25, hDialog, (HMENU)1001, hInstance, NULL);
    SendMessageW(hCheckInterval, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    hLabelReload = CreateWindowW(L"STATIC", SL(labelReload),
        WS_CHILD | WS_VISIBLE, 20, 85, 290, 20, hDialog, NULL, hInstance, NULL);
    SendMessageW(hLabelReload, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    wchar_t reloadStr[32];
    swprintf(reloadStr, 32, L"%d", reloadInterval);
    hReloadInterval = CreateWindowW(L"EDIT", reloadStr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        20, 110, 100, 25, hDialog, (HMENU)1002, hInstance, NULL);
    SendMessageW(hReloadInterval, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    hBtnSave = CreateWindowW(L"BUTTON", SL(btnSave),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 175, 70, 28, hDialog, (HMENU)IDOK, hInstance, NULL);
    SendMessageW(hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    hBtnCancel = CreateWindowW(L"BUTTON", SL(btnCancel),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        100, 175, 70, 28, hDialog, (HMENU)IDCANCEL, hInstance, NULL);
    SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Language toggle button
    hBtnLang = CreateWindowW(L"BUTTON", SL(btnLang),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        185, 175, 50, 28, hDialog, (HMENU)2010, hInstance, NULL);
    SendMessageW(hBtnLang, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    ShowWindow(hDialog, SW_SHOW);
    
    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return 0;
}
