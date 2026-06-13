#include "process_monitor.h"
#include "../utils/logger.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>

static const wchar_t *system_processes[] = {
    L"System",
    L"Idle",
    L"smss.exe",
    L"csrss.exe",
    L"wininit.exe",
    L"services.exe",
    L"lsass.exe",
    L"svchost.exe",
    L"winlogon.exe",
    L"dwm.exe",
    L"explorer.exe",
    L"RuntimeBroker.exe",
    L"SearchHost.exe",
    L"ShellExperienceHost.exe",
    L"TextInputHost.exe",
    L"StartMenuExperienceHost.exe",
    NULL
};

BOOL IsSystemProcess(const wchar_t *name) {
    for (int i = 0; system_processes[i] != NULL; i++) {
        if (_wcsicmp(name, system_processes[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

int GetAllProcesses(ProcessData **outProcesses, BOOL hideSystemProcesses) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);

    int capacity = 256;
    int count = 0;
    ProcessData *processes = malloc(capacity * sizeof(ProcessData));
    if (!processes) {
        CloseHandle(hSnap);
        return 0;
    }

    if (!Process32FirstW(hSnap, &pe)) {
        free(processes);
        CloseHandle(hSnap);
        return 0;
    }

    do {
        if (hideSystemProcesses && IsSystemProcess(pe.szExeFile)) {
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            ProcessData *tmp = realloc(processes, capacity * sizeof(ProcessData));
            if (!tmp) {
                free(processes);
                CloseHandle(hSnap);
                return count;
            }
            processes = tmp;
        }

        processes[count].pid = pe.th32ProcessID;
        processes[count].ppid = pe.th32ParentProcessID;
        wcsncpy(processes[count].name, pe.szExeFile, 255);
        processes[count].name[255] = L'\0';
        processes[count].memoryKB = 0;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                processes[count].memoryKB = pmc.WorkingSetSize / 1024;
            }
            CloseHandle(hProc);
        }

        count++;
    } while (Process32NextW(hSnap, &pe));

    CloseHandle(hSnap);
    *outProcesses = processes;
    return count;
}

BOOL TerminateProcessByPID(DWORD pid) {
    if (pid == 0 || pid == 4) return FALSE;

    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) {
        Log(LOG_ERROR, L"无法打开进程 PID %lu", pid);
        return FALSE;
    }

    BOOL result = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    return result;
}

BOOL TerminateProcessTree(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ParentProcessID == pid) {
                TerminateProcessTree(pe.th32ProcessID);
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);

    return TerminateProcessByPID(pid);
}