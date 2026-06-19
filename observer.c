/**
 * observer.c - 进程实时监控窗口
 *
 * 通过 observer.dll 采集 CPU/GPU/内存/显存 数据，
 * 以折线图展示过去30分钟的使用趋势。
 *
 * 用法: observer.exe --pid <PID>
 */

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- observer.dll 函数指针 ---- */
typedef void (*FnGetProcessStatsEx)(DWORD pid, double *cpu, double *gpu,
                                     double *memMB, double *vramMB);
static FnGetProcessStatsEx g_fnGetStats = NULL;
static HMODULE g_hDll = NULL;

#define MAX_HISTORY 90        /* 90 个数据点 × 20s = 30 分钟 */
#define UPDATE_INTERVAL 20000 /* 20 秒采集一次 */

/* 4 条历史数据曲线 */
static double g_cpuHistory[MAX_HISTORY];
static double g_gpuHistory[MAX_HISTORY];
static double g_memHistory[MAX_HISTORY];
static double g_vramHistory[MAX_HISTORY];
static int    g_count = 0;
static DWORD  g_pid = 0;

static HFONT g_hFont;
static HWND  g_hwnd;
static POINT g_mousePos = {0};
static BOOL  g_showTooltip = FALSE;
static int   g_tooltipType = 0;   /* 0=none, 1=cpu, 2=gpu, 3=mem, 4=vram */
static double g_tooltipValue = 0;

/* 图表区域定义 (y起点, 高度) */
#define CHART_H        80
#define CHART_GAP      20
#define CPU_Y          35
#define GPU_Y          (CPU_Y + CHART_H + CHART_GAP)      /* 135 */
#define MEM_Y          (GPU_Y + CHART_H + CHART_GAP)      /* 235 */
#define VRAM_Y         (MEM_Y + CHART_H + CHART_GAP)      /* 335 */
#define TIME_Y         (VRAM_Y + CHART_H + 10)             /* 425 */
#define WIN_W          520
#define WIN_H          (TIME_Y + 40)                       /* 465 */
#define CHART_X        10
#define CHART_W        (WIN_W - 20)                        /* 500 */

/* ======================== 加载 observer.dll ======================== */
static BOOL LoadObserverDll(void) {
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(NULL, dllPath, MAX_PATH);
    wchar_t *p = wcsrchr(dllPath, L'\\');
    if (p) *p = L'\0';
    wcscat(dllPath, L"\\observer.dll");

    g_hDll = LoadLibraryW(dllPath);
    if (!g_hDll) {
        /* 尝试从上级目录加载 */
        GetModuleFileNameW(NULL, dllPath, MAX_PATH);
        p = wcsrchr(dllPath, L'\\');
        if (p) *p = L'\0';
        wcscat(dllPath, L"\\observe\\observer.dll");
        g_hDll = LoadLibraryW(dllPath);
    }
    if (!g_hDll) return FALSE;

    g_fnGetStats = (FnGetProcessStatsEx)GetProcAddress(g_hDll, "GetProcessStatsEx");
    return (g_fnGetStats != NULL);
}

/* ======================== 数据采集 ======================== */
static void CollectData(void) {
    double cpu = 0, gpu = 0, mem = 0, vram = 0;
    if (g_fnGetStats)
        g_fnGetStats(g_pid, &cpu, &gpu, &mem, &vram);

    /* 滚动数组 */
    if (g_count < MAX_HISTORY) {
        g_cpuHistory[g_count]  = cpu;
        g_gpuHistory[g_count]  = gpu;
        g_memHistory[g_count]  = mem;
        g_vramHistory[g_count] = vram;
        g_count++;
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            g_cpuHistory[i]  = g_cpuHistory[i + 1];
            g_gpuHistory[i]  = g_gpuHistory[i + 1];
            g_memHistory[i]  = g_memHistory[i + 1];
            g_vramHistory[i] = g_vramHistory[i + 1];
        }
        g_cpuHistory[MAX_HISTORY - 1]  = cpu;
        g_gpuHistory[MAX_HISTORY - 1]  = gpu;
        g_memHistory[MAX_HISTORY - 1]  = mem;
        g_vramHistory[MAX_HISTORY - 1] = vram;
    }
}

/* ======================== 绘制折线图 ======================== */
static void DrawChart(HDC hdc, int x, int y, int w, int h,
                      double *data, int count, COLORREF color, double maxVal) {
    if (count < 2) return;

    /* 网格线 */
    HPEN hGridPen = CreatePen(PS_DOT, 1, RGB(60, 60, 60));
    HPEN hOldGrid = SelectObject(hdc, hGridPen);
    for (int i = 1; i < 4; i++) {
        int gy = y + h * i / 4;
        MoveToEx(hdc, x, gy, NULL);
        LineTo(hdc, x + w, gy);
    }
    SelectObject(hdc, hOldGrid);
    DeleteObject(hGridPen);

    /* 折线 */
    HPEN hPen = CreatePen(PS_SOLID, 2, color);
    HPEN hOldPen = SelectObject(hdc, hPen);
    BOOL first = TRUE;
    for (int i = 0; i < count && i < MAX_HISTORY; i++) {
        double val = (maxVal > 0) ? data[i] / maxVal : 0;
        if (val > 1.0) val = 1.0;
        if (val < 0) val = 0;
        int px = x + (int)((double)i / MAX_HISTORY * w);
        int py = y + h - (int)(val * h);
        if (py < y) py = y;
        if (py > y + h) py = y + h;
        if (first) { MoveToEx(hdc, px, py, NULL); first = FALSE; }
        else        LineTo(hdc, px, py);
    }
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);

    /* 基线 */
    HPEN hBasePen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    HPEN hOldBase = SelectObject(hdc, hBasePen);
    MoveToEx(hdc, x, y + h, NULL);
    LineTo(hdc, x + w, y + h);
    SelectObject(hdc, hOldBase);
    DeleteObject(hBasePen);
}

/* 画一个图表标题文字 */
static void DrawLabel(HDC hdc, int x, int y, const wchar_t *text, COLORREF color) {
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text, (int)wcslen(text));
}

/* 根据鼠标位置更新 tooltip */
static void UpdateTooltip(int mx, int my) {
    g_showTooltip = FALSE;

    int idx = -1;
    int chartType = 0;

    if (my >= CPU_Y && my <= CPU_Y + CHART_H &&
        mx >= CHART_X && mx <= CHART_X + CHART_W) {
        idx = (mx - CHART_X) * MAX_HISTORY / CHART_W;
        chartType = 1;
    } else if (my >= GPU_Y && my <= GPU_Y + CHART_H &&
               mx >= CHART_X && mx <= CHART_X + CHART_W) {
        idx = (mx - CHART_X) * MAX_HISTORY / CHART_W;
        chartType = 2;
    } else if (my >= MEM_Y && my <= MEM_Y + CHART_H &&
               mx >= CHART_X && mx <= CHART_X + CHART_W) {
        idx = (mx - CHART_X) * MAX_HISTORY / CHART_W;
        chartType = 3;
    } else if (my >= VRAM_Y && my <= VRAM_Y + CHART_H &&
               mx >= CHART_X && mx <= CHART_X + CHART_W) {
        idx = (mx - CHART_X) * MAX_HISTORY / CHART_W;
        chartType = 4;
    }

    if (idx >= 0 && idx < g_count) {
        g_showTooltip = TRUE;
        g_tooltipType = chartType;
        switch (chartType) {
            case 1: g_tooltipValue = g_cpuHistory[idx];  break;
            case 2: g_tooltipValue = g_gpuHistory[idx];  break;
            case 3: g_tooltipValue = g_memHistory[idx];  break;
            case 4: g_tooltipValue = g_vramHistory[idx]; break;
        }
    }
}

/* ======================== 窗口过程 ======================== */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            g_mousePos.x = (int)(short)LOWORD(lParam);
            g_mousePos.y = (int)(short)HIWORD(lParam);
            UpdateTooltip(g_mousePos.x, g_mousePos.y);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case WM_MOUSELEAVE:
            g_showTooltip = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case WM_CREATE:
            g_hwnd = hwnd;
            /* 初始 2 秒采集，积累 10 个点后切换到 20 秒 */
            SetTimer(hwnd, 1, 2000, NULL);
            {
                TRACKMOUSEEVENT tme = {0};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            CollectData();
            InvalidateRect(hwnd, NULL, TRUE);
            break;

        case WM_TIMER:
            CollectData();
            /* 积累足够数据后切换到 20 秒间隔 */
            {
                static BOOL switched = FALSE;
                if (!switched && g_count >= 10) {
                    switched = TRUE;
                    KillTimer(hwnd, 1);
                    SetTimer(hwnd, 1, UPDATE_INTERVAL, NULL);
                }
            }
            InvalidateRect(hwnd, NULL, TRUE);
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            /* 背景 */
            HBRUSH hBrush = CreateSolidBrush(RGB(25, 25, 30));
            FillRect(hdc, &ps.rcPaint, hBrush);
            DeleteObject(hBrush);

            /* 字体 */
            g_hFont = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");
            SelectObject(hdc, g_hFont);
            SetBkMode(hdc, TRANSPARENT);

            /* ---- CPU 图表 ---- */
            {
                wchar_t s[64];
                swprintf(s, 64, L"CPU: %.1f%%", g_count > 0 ? g_cpuHistory[g_count - 1] : 0);
                DrawLabel(hdc, CHART_X, CPU_Y - 22, s, RGB(0, 255, 100));
                DrawChart(hdc, CHART_X, CPU_Y, CHART_W, CHART_H,
                          g_cpuHistory, g_count, RGB(0, 255, 100), 100.0);
            }
            /* ---- GPU 图表 ---- */
            {
                wchar_t s[64];
                swprintf(s, 64, L"GPU: %.1f%%", g_count > 0 ? g_gpuHistory[g_count - 1] : 0);
                DrawLabel(hdc, CHART_X, GPU_Y - 22, s, RGB(180, 100, 255));
                DrawChart(hdc, CHART_X, GPU_Y, CHART_W, CHART_H,
                          g_gpuHistory, g_count, RGB(180, 100, 255), 100.0);
            }
            /* ---- 内存 图表 ---- */
            {
                double maxMem = 0;
                for (int i = 0; i < g_count; i++)
                    if (g_memHistory[i] > maxMem) maxMem = g_memHistory[i];
                if (maxMem < 100) maxMem = 100;
                wchar_t s[64];
                swprintf(s, 64, L"内存: %.1f MB", g_count > 0 ? g_memHistory[g_count - 1] : 0);
                DrawLabel(hdc, CHART_X, MEM_Y - 22, s, RGB(100, 180, 255));
                DrawChart(hdc, CHART_X, MEM_Y, CHART_W, CHART_H,
                          g_memHistory, g_count, RGB(100, 180, 255), maxMem);
            }
            /* ---- 显存 图表 ---- */
            {
                double maxVram = 0;
                for (int i = 0; i < g_count; i++)
                    if (g_vramHistory[i] > maxVram) maxVram = g_vramHistory[i];
                if (maxVram < 100) maxVram = 100;
                wchar_t s[64];
                swprintf(s, 64, L"显存: %.1f MB", g_count > 0 ? g_vramHistory[g_count - 1] : 0);
                DrawLabel(hdc, CHART_X, VRAM_Y - 22, s, RGB(255, 180, 80));
                DrawChart(hdc, CHART_X, VRAM_Y, CHART_W, CHART_H,
                          g_vramHistory, g_count, RGB(255, 180, 80), maxVram);
            }
            /* ---- 时间轴标签 ---- */
            {
                HFONT hSmall = CreateFontW(10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH, L"Segoe UI");
                SelectObject(hdc, hSmall);
                SetTextColor(hdc, RGB(100, 100, 100));
                for (int i = 0; i <= 30; i += 10) {
                    int px = CHART_X + CHART_W * i / 30;
                    wchar_t t[16];
                    swprintf(t, 16, L"-%dmin", 30 - i);
                    TextOutW(hdc, px - 15, TIME_Y, t, (int)wcslen(t));
                }
                DeleteObject(hSmall);
            }

            /* ---- Tooltip ---- */
            if (g_showTooltip) {
                wchar_t tipStr[64];
                switch (g_tooltipType) {
                    case 1: swprintf(tipStr, 64, L"CPU: %.1f%%", g_tooltipValue); break;
                    case 2: swprintf(tipStr, 64, L"GPU: %.1f%%", g_tooltipValue); break;
                    case 3: swprintf(tipStr, 64, L"内存: %.1f MB", g_tooltipValue); break;
                    case 4: swprintf(tipStr, 64, L"显存: %.1f MB", g_tooltipValue); break;
                    default: wcscpy(tipStr, L""); break;
                }

                int tipW = 140, tipH = 25;
                int tipX = g_mousePos.x + 10;
                int tipY = g_mousePos.y + 10;
                if (tipX + tipW > WIN_W) tipX = g_mousePos.x - tipW - 10;
                if (tipY + tipH > WIN_H) tipY = g_mousePos.y - tipH - 10;

                HBRUSH hTipBrush = CreateSolidBrush(RGB(50, 50, 55));
                HPEN hTipPen = CreatePen(PS_SOLID, 1, RGB(100, 200, 255));
                SelectObject(hdc, hTipBrush);
                SelectObject(hdc, hTipPen);
                Rectangle(hdc, tipX, tipY, tipX + tipW, tipY + tipH);
                DeleteObject(hTipBrush);
                DeleteObject(hTipPen);

                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkColor(hdc, RGB(50, 50, 55));
                TextOutW(hdc, tipX + 8, tipY + 5, tipStr, (int)wcslen(tipStr));
            }

            DeleteObject(g_hFont);
            EndPaint(hwnd, &ps);
            break;
        }

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

/* ======================== WinMain ======================== */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    /* 解析 --pid 参数 */
    if (lpCmdLine) {
        wchar_t *p = wcsstr(lpCmdLine, L"--pid");
        if (p) {
            p += 5;
            while (*p == L' ' || *p == L'=') p++;
            g_pid = (DWORD)wcstoul(p, NULL, 10);
        }
    }
    if (g_pid == 0) {
        MessageBoxW(NULL, L"请指定进程PID: observer.exe --pid <数字>",
                    L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* 加载 observer.dll */
    if (!LoadObserverDll()) {
        wchar_t msg[512];
        swprintf(msg, 512,
                 L"无法加载 observer.dll！\n\n"
                 L"请确保 observer.dll 与 observer.exe 在同一目录。\n"
                 L"错误码: %lu", GetLastError());
        MessageBoxW(NULL, msg, L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* 获取进程名作为窗口标题 */
    wchar_t title[128] = L"实时监控 - ";
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {0};
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == g_pid) {
                    wchar_t pidStr[32];
                    swprintf(pidStr, 32, L" (PID:%lu)", g_pid);
                    wcscat(title, pe.szExeFile);
                    wcscat(title, pidStr);
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = CreateSolidBrush(RGB(25, 25, 30));
    wc.lpszClassName = L"ObserverWin";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title,
                                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                WIN_W + 16, WIN_H + 38,
                                NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hDll) FreeLibrary(g_hDll);
    return 0;
}
