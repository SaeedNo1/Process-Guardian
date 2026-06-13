#include "gui.h"
#include "../core/process_monitor.h"
#include "../core/process_protector.h"
#include "../utils/logger.h"
#include <commctrl.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#define IDC_PROCESS_LIST 1001
#define IDC_HIDE_SYSTEM 1002

static HWND g_hProcessList;
static HWND g_hHideSystemCheck;
static HFONT g_hFont;
static HWND g_hMainWindow;

void SetMainWindow(HWND hwnd) {
    g_hMainWindow = hwnd;
}

LRESULT OnCreate(HWND hwnd) {
    g_hMainWindow = hwnd;
    
    g_hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    g_hHideSystemCheck = CreateWindowW(L"button", L"隐藏系统进程?",
                                       WS_CHILD | WS_VISIBLE | BS_CHECKBOX,
                                       10, 10, 200, 25, hwnd,
                                       (HMENU)IDC_HIDE_SYSTEM, GetModuleHandle(NULL), NULL);
    SendMessageW(g_hHideSystemCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(g_hHideSystemCheck, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hProcessList = CreateWindowExW(0, L"SysListView32", L"",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SORTASCENDING,
                                     10, 45, 760, 490, hwnd,
                                     (HMENU)IDC_PROCESS_LIST, GetModuleHandle(NULL), NULL);
    SendMessageW(g_hProcessList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    LVCOLUMNW col;
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = L"PID";
    col.cx = 80;
    ListView_InsertColumn(g_hProcessList, 0, &col);
    col.pszText = L"进程名称";
    col.cx = 250;
    ListView_InsertColumn(g_hProcessList, 1, &col);
    col.pszText = L"内存 (KB)";
    col.cx = 100;
    ListView_InsertColumn(g_hProcessList, 2, &col);
    col.pszText = L"状态";
    col.cx = 100;
    ListView_InsertColumn(g_hProcessList, 3, &col);

    ListView_SetExtendedListViewStyle(g_hProcessList, LVS_EX_FULLROWSELECT);

    RefreshProcessList(hwnd, FALSE);

    SetTimer(hwnd, 1, 2000, NULL);

    return 0;
}

LRESULT OnCommand(HWND hwnd, WPARAM wParam) {
    if (LOWORD(wParam) == IDC_HIDE_SYSTEM) {
        BOOL hideSystem = (SendMessageW(g_hHideSystemCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
        RefreshProcessList(hwnd, hideSystem);
    }
    return 0;
}

LRESULT OnContextMenu(HWND hwnd, LPARAM lParam) {
    int x = GET_X_LPARAM(lParam);
    int y = GET_Y_LPARAM(lParam);

    POINT pt = {x, y};
    ScreenToClient(hwnd, &pt);

    HWND hList = ChildWindowFromPoint(hwnd, pt);
    if (hList != g_hProcessList) return 0;

    int sel = ListView_GetNextItem(g_hProcessList, -1, LVNI_SELECTED);
    if (sel == -1) return 0;

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"结束进程");
    AppendMenuW(hMenu, MF_STRING, 2, L"结束进程树");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, L"");
    AppendMenuW(hMenu, MF_STRING, 3, L"重复结束进程");
    AppendMenuW(hMenu, MF_STRING, 4, L"重复结束进程树");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, L"");
    AppendMenuW(hMenu, MF_STRING, 5, L"保护进程");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, L"");
    AppendMenuW(hMenu, MF_STRING, 6, L"查看被保护的进程");

    int cmd = TrackPopupMenuW(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);

    ProcessInfo pi;
    if (GetSelectedProcess(hwnd, &pi)) {
        HandleContextMenuAction(hwnd, cmd, &pi);
    }
    DestroyMenu(hMenu);

    return 0;
}

LRESULT OnTimer(HWND hwnd, WPARAM wParam) {
    if (wParam == 1) {
        BOOL hideSystem = (SendMessageW(g_hHideSystemCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
        RefreshProcessList(hwnd, hideSystem);
    }
    return 0;
}

void HandleContextMenuAction(HWND hwnd, int cmd, ProcessInfo *pi) {
    switch (cmd) {
        case 1:
            TerminateProcessByPID(pi->pid);
            Log(LOG_INFO, L"结束进程: %s (PID: %lu)", pi->name, pi->pid);
            break;
        case 2:
            TerminateProcessTree(pi->pid);
            Log(LOG_INFO, L"结束进程树: %s (PID: %lu)", pi->name, pi->pid);
            break;
        case 3:
            AddToRepeatedList(pi->name);
            Log(LOG_INFO, L"添加重复结束: %s", pi->name);
            break;
        case 4:
            AddToRepeatedTreeList(pi->name);
            Log(LOG_INFO, L"添加重复结束进程树: %s", pi->name);
            break;
        case 5:
            AddToProtectedList(pi->name);
            Log(LOG_INFO, L"保护进程: %s", pi->name);
            break;
        case 6:
            ShowProtectedProcessesDialog(hwnd);
            break;
    }
    BOOL hideSystem = (SendMessageW(g_hHideSystemCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    RefreshProcessList(hwnd, hideSystem);
}

void ShowProtectedProcessesDialog(HWND hwnd) {
    wchar_t *list = GetProtectedProcessesList();
    MessageBoxW(hwnd, list ? list : L"无保护的进程", L"被保护的进程", MB_OK);
}

void RefreshProcessList(HWND hwnd, BOOL hideSystemProcesses) {
    (void)hwnd;
    ListView_DeleteAllItems(g_hProcessList);

    ProcessData *processes = NULL;
    int count = GetAllProcesses(&processes, hideSystemProcesses);

    LVITEMW lvi;
    lvi.mask = LVIF_TEXT;

    for (int i = 0; i < count; i++) {
        lvi.iItem = i;
        lvi.iSubItem = 0;
        wchar_t pidStr[32];
        swprintf(pidStr, 32, L"%lu", (unsigned long)processes[i].pid);
        lvi.pszText = pidStr;
        ListView_InsertItem(g_hProcessList, &lvi);

        lvi.iSubItem = 1;
        lvi.pszText = processes[i].name;
        ListView_SetItem(g_hProcessList, &lvi);

        lvi.iSubItem = 2;
        wchar_t memStr[32];
        swprintf(memStr, 32, L"%lu", (unsigned long)processes[i].memoryKB);
        lvi.pszText = memStr;
        ListView_SetItem(g_hProcessList, &lvi);

        lvi.iSubItem = 3;
        if (IsProcessProtected(processes[i].name)) {
            lvi.pszText = L"已保护";
        } else if (IsProcessRepeated(processes[i].name)) {
            lvi.pszText = L"重复终止";
        } else {
            lvi.pszText = L"";
        }
        ListView_SetItem(g_hProcessList, &lvi);
    }

    if (processes) free(processes);
}

BOOL GetSelectedProcess(HWND hwnd, ProcessInfo *info) {
    (void)hwnd;
    int sel = ListView_GetNextItem(g_hProcessList, -1, LVNI_SELECTED);
    if (sel == -1) return FALSE;

    LVITEMW lvi;
    lvi.mask = LVIF_TEXT;
    lvi.iItem = sel;
    lvi.iSubItem = 0;
    wchar_t pidStr[32];
    lvi.pszText = pidStr;
    lvi.cchTextMax = 32;
    ListView_GetItem(g_hProcessList, &lvi);
    info->pid = wcstoul(pidStr, NULL, 10);

    lvi.iSubItem = 1;
    lvi.pszText = info->name;
    lvi.cchTextMax = 256;
    ListView_GetItem(g_hProcessList, &lvi);

    return TRUE;
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            return OnCreate(hwnd);
        case WM_COMMAND:
            return OnCommand(hwnd, wParam);
        case WM_CONTEXTMENU:
            return OnContextMenu(hwnd, lParam);
        case WM_TIMER:
            return OnTimer(hwnd, wParam);
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

HWND InitGUI(HINSTANCE hInstance, int nCmdShow) {
    (void)hInstance;
    (void)nCmdShow;
    return NULL;
}