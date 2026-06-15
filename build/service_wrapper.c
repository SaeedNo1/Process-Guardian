#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>

static SERVICE_STATUS g_serviceStatus;
static SERVICE_STATUS_HANDLE g_serviceHandle;
static HANDLE g_stopEvent = NULL;

void WINAPI ServiceCtrl(DWORD ctrlCode) {
    if (ctrlCode == SERVICE_CONTROL_STOP) {
        g_serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_serviceHandle, &g_serviceStatus);
        if (g_stopEvent) SetEvent(g_stopEvent);
    }
}

// Get directory containing this service_wrapper.exe
static void GetServiceDir(wchar_t *out, int outLen) {
    GetModuleFileNameW(NULL, out, outLen);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = L'\0';
    // If we're inside a \build\ folder, go up one level
    wchar_t *buildPos = wcsstr(out, L"\\build");
    if (buildPos) *buildPos = L'\0';
}

// Find running guardiand.exe (not the one we are about to spawn)
static BOOL IsGuardiandRunning(DWORD myPid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    BOOL found = FALSE;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"guardiand.exe") == 0 &&
                pe.th32ParentProcessID != myPid) {
                found = TRUE;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv) {
    (void)argc;
    (void)argv;
    
    g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_serviceStatus.dwWin32ExitCode = 0;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    g_serviceStatus.dwCheckPoint = 0;
    g_serviceStatus.dwWaitHint = 3000;
    
    g_serviceHandle = RegisterServiceCtrlHandlerW(L"GuardianDaemon", ServiceCtrl);
    if (g_serviceHandle) {
        SetServiceStatus(g_serviceHandle, &g_serviceStatus);
    }
    
    // Path to guardiand.exe
    wchar_t exePath[MAX_PATH];
    GetServiceDir(exePath, MAX_PATH);
    wcscat(exePath, L"\\guardiand.exe");
    
    // Start the actual daemon - NO console, fully detached
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    
    // CREATE_NO_WINDOW + DETACHED_PROCESS: 子进程没有 console，不会出现黑窗
    if (CreateProcessW(exePath, L" --hidden", NULL, NULL, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                       NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    // Mark as running
    g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
    g_serviceStatus.dwCheckPoint = 0;
    g_serviceStatus.dwWaitHint = 0;
    SetServiceStatus(g_serviceHandle, &g_serviceStatus);
    
    // Monitor loop: ensure guardiand is alive; restart if dead
    while (WaitForSingleObject(g_stopEvent, 5000) == WAIT_TIMEOUT) {
        if (!IsGuardiandRunning(GetCurrentProcessId())) {
            STARTUPINFOW si2 = {0};
            si2.cb = sizeof(si2);
            si2.dwFlags = STARTF_USESHOWWINDOW;
            si2.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi2 = {0};
            if (CreateProcessW(exePath, L" --hidden", NULL, NULL, FALSE,
                               CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                               NULL, NULL, &si2, &pi2)) {
                CloseHandle(pi2.hProcess);
                CloseHandle(pi2.hThread);
            }
        }
    }
    
    // Service is stopping
    g_serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    g_serviceStatus.dwWaitHint = 3000;
    SetServiceStatus(g_serviceHandle, &g_serviceStatus);
    
    // Kill any remaining guardiand.exe spawned by us
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {0};
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"guardiand.exe") == 0 &&
                    pe.th32ParentProcessID == GetCurrentProcessId()) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    
    Sleep(1000);
    
    g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_serviceHandle, &g_serviceStatus);
    if (g_stopEvent) CloseHandle(g_stopEvent);
}

int wmain(void) {
    SERVICE_TABLE_ENTRYW ste[] = {
        { L"GuardianDaemon", ServiceMain },
        { NULL, NULL }
    };
    
    StartServiceCtrlDispatcherW(ste);
    return 0;
}
