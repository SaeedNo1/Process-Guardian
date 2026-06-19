#ifndef PROCESS_MONITOR_H
#define PROCESS_MONITOR_H

#include <windows.h>

typedef struct {
    DWORD pid;
    DWORD ppid;
    wchar_t name[256];
    DWORD memoryKB;
} ProcessData;

// Get all running processes
int GetAllProcesses(ProcessData **outProcesses, BOOL hideSystemProcesses);

// Terminate a process by PID
BOOL TerminateProcessByPID(DWORD pid);

// Terminate a process tree (parent and all children)
BOOL TerminateProcessTree(DWORD pid);

// Check if process is system process
BOOL IsSystemProcess(const wchar_t *name);

#endif