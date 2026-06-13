#define UNICODE
#define _UNICODE

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_HISTORY 90
#define UPDATE_INTERVAL 20000

static double g_cpuHistory[MAX_HISTORY];
static double g_memHistory[MAX_HISTORY];
static int g_count = 0;
static DWORD g_pid = 0;
static HFONT g_hFont;
static HWND g_hwnd;
static POINT g_mousePos = {0};
static BOOL g_showTooltip = FALSE;
static int g_tooltipType = 0;  // 0=none, 1=cpu, 2=mem
static double g_tooltipValue = 0;
static int g_windowW = 500;
static int g_windowH = 320;

void GetProcessStats(DWORD pid, double *cpu, double *memMB, double *cacheMB) {
    *cpu = 0;
    *memMB = 0;
    *cacheMB = 0;
    
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return;
    
    // Memory - Working Set
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
        *memMB = pmc.WorkingSetSize / (1024.0 * 1024.0);
        *cacheMB = pmc.WorkingSetSize / (1024.0 * 1024.0);  // Use WS as cache indicator
    }
    
    // CPU
    FILETIME ftCreation, ftExit, ftKernel, ftUser;
    if (GetProcessTimes(hProc, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
        static ULONGLONG lastTime = 0;
        static ULONGLONG lastKernel = 0;
        static ULONGLONG lastUser = 0;
        
        ULONGLONG kernel = ((ULONGLONG)ftKernel.dwHighDateTime << 32) | ftKernel.dwLowDateTime;
        ULONGLONG user = ((ULONGLONG)ftUser.dwHighDateTime << 32) | ftUser.dwLowDateTime;
        ULONGLONG now = GetTickCount64();
        
        if (lastTime > 0 && now > lastTime) {
            ULONGLONG timeDiff = now - lastTime;
            ULONGLONG cpuDiff = (kernel - lastKernel) + (user - lastUser);
            if (timeDiff > 0) {
                *cpu = (double)cpuDiff / (timeDiff * 10000.0) * 100.0;
                if (*cpu > 100) *cpu = 100;
            }
        }
        
        lastTime = now;
        lastKernel = kernel;
        lastUser = user;
    }
    
    CloseHandle(hProc);
}

static double g_cacheHistory[MAX_HISTORY];

void AddDataPoint(double cpu, double mem, double cache) {
    if (g_count < MAX_HISTORY) {
        g_cpuHistory[g_count] = cpu;
        g_memHistory[g_count] = mem;
        g_cacheHistory[g_count] = cache;
        g_count++;
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            g_cpuHistory[i] = g_cpuHistory[i + 1];
            g_memHistory[i] = g_memHistory[i + 1];
            g_cacheHistory[i] = g_cacheHistory[i + 1];
        }
        g_cpuHistory[MAX_HISTORY - 1] = cpu;
        g_memHistory[MAX_HISTORY - 1] = mem;
        g_cacheHistory[MAX_HISTORY - 1] = cache;
    }
}

void DrawPolyline(HDC hdc, int x, int y, int w, int h, double *data, int count, COLORREF color, double maxVal) {
    if (count < 2) return;
    
    HPEN hPen = CreatePen(PS_SOLID, 2, color);
    HPEN hOldPen = SelectObject(hdc, hPen);
    
    // Draw grid
    HPEN hGridPen = CreatePen(PS_DOT, 1, RGB(60, 60, 60));
    HPEN hOldGrid = SelectObject(hdc, hGridPen);
    for (int i = 1; i < 4; i++) {
        int gy = y + h * i / 4;
        MoveToEx(hdc, x, gy, NULL);
        LineTo(hdc, x + w, gy);
    }
    SelectObject(hdc, hOldGrid);
    DeleteObject(hGridPen);
    
    // Draw line - start from first data point
    BOOL first = TRUE;
    for (int i = 0; i < count && i < MAX_HISTORY; i++) {
        double val = data[i];
        if (maxVal > 0) val = val / maxVal;
        if (val > 1.0) val = 1.0;  // Clamp to 0-1 range
        int px = x + (int)((double)i / MAX_HISTORY * w);
        int py = y + h - (int)(val * h);
        if (py < y) py = y;
        if (py > y + h) py = y + h;
        
        if (first) {
            MoveToEx(hdc, px, py, NULL);
            first = FALSE;
        } else {
            LineTo(hdc, px, py);
        }
    }
    
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    // Draw baseline at y+h
    HPEN hBasePen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    HPEN hOldBase = SelectObject(hdc, hBasePen);
    MoveToEx(hdc, x, y + h, NULL);
    LineTo(hdc, x + w, y + h);
    SelectObject(hdc, hOldBase);
    DeleteObject(hBasePen);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE:
            {
                int x = (int)(short)LOWORD(lParam);
                int y = (int)(short)HIWORD(lParam);
                g_mousePos.x = x;
                g_mousePos.y = y;
                
                // Check which graph area
                if (y >= 35 && y <= 115 && x >= 10 && x <= 480) {
                    // CPU graph area
                    int idx = (x - 10) * MAX_HISTORY / 470;
                    if (idx >= 0 && idx < g_count) {
                        g_showTooltip = TRUE;
                        g_tooltipType = 1;
                        g_tooltipValue = g_cpuHistory[idx];
                    } else {
                        g_showTooltip = FALSE;
                    }
                } else if (y >= 155 && y <= 235 && x >= 10 && x <= 480) {
                    // Memory graph area
                    int idx = (x - 10) * MAX_HISTORY / 470;
                    if (idx >= 0 && idx < g_count) {
                        g_showTooltip = TRUE;
                        g_tooltipType = 2;
                        g_tooltipValue = g_memHistory[idx];
                    } else {
                        g_showTooltip = FALSE;
                    }
                } else if (y >= 250 && y <= 290 && x >= 10 && x <= 480) {
                    // Cache graph area
                    int idx = (x - 10) * MAX_HISTORY / 470;
                    if (idx >= 0 && idx < g_count) {
                        g_showTooltip = TRUE;
                        g_tooltipType = 3;
                        g_tooltipValue = g_cacheHistory[idx];
                    } else {
                        g_showTooltip = FALSE;
                    }
                } else {
                    g_showTooltip = FALSE;
                }
                
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
            
        case WM_MOUSELEAVE:
            g_showTooltip = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case WM_CREATE:
            g_hwnd = hwnd;
            // Use 2 second interval initially, switch to 20s after we have enough data
            SetTimer(hwnd, 1, 2000, NULL);
            
            // Track mouse for leave event
            {
                TRACKMOUSEEVENT tme = {0};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            {
                double cpu, mem, cache;
                GetProcessStats(g_pid, &cpu, &mem, &cache);
                AddDataPoint(cpu, mem, cache);
                // Force redraw
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
            
        case WM_TIMER:
            {
                double cpu, mem, cache;
                GetProcessStats(g_pid, &cpu, &mem, &cache);
                AddDataPoint(cpu, mem, cache);
                
                // Switch to 20 second interval after collecting enough points
                static BOOL switched = FALSE;
                if (!switched && g_count >= 10) {
                    switched = TRUE;
                    KillTimer(hwnd, 1);
                    SetTimer(hwnd, 1, 20000, NULL);
                }
                
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
            
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                
                int w = ps.rcPaint.right - ps.rcPaint.left;
                int h = ps.rcPaint.bottom - ps.rcPaint.top;
                
                // Background
                HBRUSH hBrush = CreateSolidBrush(RGB(25, 25, 30));
                FillRect(hdc, &ps.rcPaint, hBrush);
                DeleteObject(hBrush);
                
                // Title
                g_hFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                SelectObject(hdc, g_hFont);
                
                SetBkMode(hdc, TRANSPARENT);
                
                // CPU Graph (green)
                SetTextColor(hdc, RGB(0, 255, 100));
                wchar_t cpuStr[64];
                swprintf(cpuStr, 64, L"CPU: %.1f%%", g_count > 0 ? g_cpuHistory[g_count-1] : 0);
                TextOutW(hdc, 10, 10, cpuStr, wcslen(cpuStr));
                DrawPolyline(hdc, 10, 35, w - 20, 80, g_cpuHistory, g_count, RGB(0, 255, 100), 100);
                
                // Memory Graph (blue)
                SetTextColor(hdc, RGB(100, 180, 255));
                wchar_t memStr[64];
                double maxMem = 0;
                for (int i = 0; i < g_count; i++) if (g_memHistory[i] > maxMem) maxMem = g_memHistory[i];
                if (maxMem < 100) maxMem = 100;
                swprintf(memStr, 64, L"内存: %.1fMB", g_count > 0 ? g_memHistory[g_count-1] : 0);
                TextOutW(hdc, 10, 130, memStr, wcslen(memStr));
                DrawPolyline(hdc, 10, 155, w - 20, 80, g_memHistory, g_count, RGB(100, 180, 255), maxMem);
                
                // Cache Graph (yellow)
                SetTextColor(hdc, RGB(255, 230, 100));
                wchar_t cacheStr[64];
                double maxCache = 0;
                for (int i = 0; i < g_count; i++) if (g_cacheHistory[i] > maxCache) maxCache = g_cacheHistory[i];
                if (maxCache < 100) maxCache = 100;
                swprintf(cacheStr, 64, L"缓存: %.1fMB", g_count > 0 ? g_cacheHistory[g_count-1] : 0);
                TextOutW(hdc, 10, 250, cacheStr, wcslen(cacheStr));
                DrawPolyline(hdc, 10, 275, w - 20, 80, g_cacheHistory, g_count, RGB(255, 230, 100), maxCache);
                
                // Time labels
                SetTextColor(hdc, RGB(100, 100, 100));
                SelectObject(hdc, g_hFont = CreateFontW(10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
                for (int i = 0; i <= 30; i += 10) {
                    int px = 10 + (w - 20) * i / 30;
                    wchar_t t[16];
                    swprintf(t, 16, L"-%dmin", 30 - i);
                    TextOutW(hdc, px - 15, h - 20, t, wcslen(t));
                }
                
                // Tooltip
                if (g_showTooltip) {
                    wchar_t tipStr[64];
                    if (g_tooltipType == 1) {
                        swprintf(tipStr, 64, L"CPU: %.1f%%", g_tooltipValue);
                    } else if (g_tooltipType == 2) {
                        swprintf(tipStr, 64, L"内存: %.1fMB", g_tooltipValue);
                    } else if (g_tooltipType == 3) {
                        swprintf(tipStr, 64, L"缓存: %.1fMB", g_tooltipValue);
                    }
                    
                    int tipW = 120, tipH = 25;
                    int tipX = g_mousePos.x + 10;
                    int tipY = g_mousePos.y + 10;
                    if (tipX + tipW > w) tipX = g_mousePos.x - tipW - 10;
                    if (tipY + tipH > h) tipY = g_mousePos.y - tipH - 10;
                    
                    // Tooltip background
                    HBRUSH hTipBrush = CreateSolidBrush(RGB(50, 50, 55));
                    HPEN hTipPen = CreatePen(PS_SOLID, 1, RGB(100, 200, 255));
                    SelectObject(hdc, hTipBrush);
                    SelectObject(hdc, hTipPen);
                    Rectangle(hdc, tipX, tipY, tipX + tipW, tipY + tipH);
                    DeleteObject(hTipBrush);
                    DeleteObject(hTipPen);
                    
                    // Tooltip text
                    SetTextColor(hdc, RGB(255, 255, 255));
                    SetBkColor(hdc, RGB(50, 50, 55));
                    TextOutW(hdc, tipX + 5, tipY + 5, tipStr, wcslen(tipStr));
                }
                
                EndPaint(hwnd, &ps);
            }
            break;
            
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    
    if (lpCmdLine) {
        wchar_t *p = wcsstr(lpCmdLine, L"--pid");
        if (p) {
            p += 6;
            while (*p == L' ') p++;
            g_pid = wcstoul(p, NULL, 10);
        }
    }
    
    if (g_pid == 0) {
        MessageBoxW(NULL, L"请指定进程PID: observer.exe --pid <数字>", L"错误", MB_OK);
        return 1;
    }
    
    // Get process name
    wchar_t title[128] = L"实时监控 - ";
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {0};
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == g_pid) {
                    wcscat(title, pe.szExeFile);
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = CreateSolidBrush(RGB(25, 25, 30));
    wc.lpszClassName = L"ObserverWin";
    RegisterClassExW(&wc);
    
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title,
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                500, 400, NULL, NULL, hInstance, NULL);
    
    ShowWindow(hwnd, nCmdShow);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return 0;
}