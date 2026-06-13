#ifndef PROCESS_PROTECTOR_H
#define PROCESS_PROTECTOR_H

#include <windows.h>

// Add process to repeated kill list
void AddToRepeatedList(const wchar_t *name);

// Add process tree to repeated kill list
void AddToRepeatedTreeList(const wchar_t *name);

// Add process to protected list
void AddToProtectedList(const wchar_t *name);

// Remove from repeated list
void RemoveFromRepeatedList(const wchar_t *name);

// Remove from protected list
void RemoveFromProtectedList(const wchar_t *name);

// Check if process is in repeated list
BOOL IsProcessRepeated(const wchar_t *name);

// Check if process is in protected list
BOOL IsProcessProtected(const wchar_t *name);

// Get list of all protected/repeated processes
wchar_t* GetProtectedProcessesList(void);

// Check and perform repeated kill / auto-restart
void CheckProcesses(void);

// Save config to file
void SaveConfig(void);

// Load config from file
void LoadConfig(void);

// Initialize protector
void InitProtector(void);

// Start the monitor thread
void StartMonitorThread(HWND hwnd);

// Stop the monitor thread
void StopMonitorThread(void);

#endif