#ifndef GUI_H
#define GUI_H

#include <windows.h>

// Initialize GUI
HWND InitGUI(HINSTANCE hInstance, int nCmdShow);

// Refresh process list
void RefreshProcessList(HWND hwnd, BOOL hideSystemProcesses);

// Process info structure
typedef struct {
    DWORD pid;
    wchar_t name[256];
} ProcessInfo;

// Get selected process info
BOOL GetSelectedProcess(HWND hwnd, ProcessInfo *info);

// Handle context menu action
void HandleContextMenuAction(HWND hwnd, int cmd, ProcessInfo *pi);

// Show protected processes dialog
void ShowProtectedProcessesDialog(HWND hwnd);

#endif